#include <Disks/DiskFromAST.h>
#include <Disks/DiskSelector.h>
#include <Disks/IStoragePolicy.h>
#include <Disks/getDiskConfigurationFromAST.h>
#include <IO/WriteHelpers.h>
#include <Interpreters/Context.h>
#include <Parsers/ASTExpressionList.h>
#include <Parsers/ASTFunction.h>
#include <Parsers/ASTIdentifier.h>
#include <Parsers/ASTLiteral.h>
#include <Parsers/IAST.h>
#include <Parsers/isDiskFunction.h>
#include <Common/Config/ConfigProcessor.h>
#include <Common/NamedCollections/NamedCollectionConfiguration.h>
#include <Common/SipHash.h>
#include <Common/ZooKeeper/ZooKeeperNodeCache.h>
#include <Common/assert_cast.h>
#include <Common/checkStackSize.h>
#include <Common/filesystemHelpers.h>

#include <algorithm>
#include <memory>
#include <set>

namespace DB
{

namespace ErrorCodes
{
extern const int ACCESS_DENIED;
extern const int BAD_ARGUMENTS;
}

/// Resolve `include`, `from_env` and `from_zk` in a disk definition.
static Poco::AutoPtr<Poco::Util::XMLConfiguration>
resolveDiskConfiguration(Poco::AutoPtr<Poco::XML::Document> xml_document, ContextPtr context, bool attach)
{
    Poco::AutoPtr<Poco::XML::NamePool> name_pool(new Poco::XML::NamePool());
    Poco::XML::DOMParser dom_parser(name_pool);
    std::vector<std::pair<std::string, std::string>> substitutions;
    zkutil::ZooKeeperNodeCache zk_node_cache([&]() { return context->getZooKeeper(); });

    ConfigProcessor::processIncludes(
        xml_document,
        substitutions,
        context->getConfigRef().getString("include_from", ""),
        /* throw_on_bad_incl= */ !attach,
        dom_parser,
        getLogger("getOrCreateCustomDisk"),
        /*contributing_zk_paths=*/{},
        /*contributing_files=*/{},
        &zk_node_cache);

    Poco::AutoPtr<Poco::Util::XMLConfiguration> config(new Poco::Util::XMLConfiguration());
    config->load(xml_document);
    return config;
}

static std::string getCredentialsMarker(const ASTPtr & arg)
{
    const auto * eq = arg->as<ASTFunction>();
    if (!eq || eq->name != "equals" || !eq->arguments || eq->arguments->children.size() != 2)
        return {};
    const auto * key = eq->arguments->children[0]->as<ASTIdentifier>();
    if (key && key->name() == S3_SERVER_CREDENTIALS_MARKER)
        return key->name();
    return {};
}

static bool hasCredentialsMarker(const ASTs & args, const std::string & marker)
{
    return std::ranges::any_of(args, [&](const ASTPtr & arg) { return getCredentialsMarker(arg) == marker; });
}

/// Match registered identities before creating a disk. The other forms differ only in credential markers and the
/// equivalent registered disks below a wrapper. A legacy form is never used to create a new backend.
static std::vector<std::pair<size_t, std::string>> getOrCreateCustomDisk(
    const ASTs & disk_args, const std::vector<std::string> & serializations, ContextPtr context, bool attach, bool for_system_database)
{
    Poco::AutoPtr<Poco::Util::XMLConfiguration> config;
    bool uses_server_credentials = false;
    bool restriction_exempt = false;
    {
        DynamicS3DiskCredentialInfo info;
        auto xml_document = getDiskConfigurationFromASTImpl(disk_args, context, attach, &info, for_system_database);
        config = resolveDiskConfiguration(xml_document, context, attach);
        if (info.load_anonymously)
            forceAnonymousS3DiskConfig(*config);
        else
            validateResolvedS3DiskCredentials(*config, context, attach, info);
        uses_server_credentials = diskUsesServerCredentials(*config, info);
        restriction_exempt = info.restriction_exempt;
    }

    const bool restricted = !attach && !restriction_exempt && context->shouldRestrictUserQueryS3Credentials();
    Poco::Util::AbstractConfiguration::Keys disk_settings_keys;
    config->keys(disk_settings_keys);
    if (disk_settings_keys.empty())
        throw Exception(ErrorCodes::BAD_ARGUMENTS, "Disk function must have arguments. Invalid disk description.");
    if (disk_settings_keys.size() == 1 && disk_settings_keys.front() == "name" && !attach)
        throw Exception(
            ErrorCodes::BAD_ARGUMENTS,
            "Disk function `{}` must have other arguments apart from `name`, which describe disk configuration. Invalid disk description.",
            serializations.front());

    auto get_disk_name = [&](UInt128 hash) -> std::string
    {
        if (config->has("name"))
            return config->getString("name");
        return DiskSelector::TMP_INTERNAL_DISK_PREFIX + toString(hash);
    };

    std::vector<UInt128> hashes;
    for (const auto & serialization : serializations)
        hashes.push_back(sipHash128(serialization.data(), serialization.size()));

    std::vector<std::pair<size_t, std::string>> matches;
    if (!attach)
    {
        const auto disks = context->getDisksMap();
        for (size_t i = 0; i < serializations.size(); ++i)
        {
            auto name = get_disk_name(hashes[i]);
            const auto it = disks.find(name);
            if (it == disks.end() || !it->second->isCustomDisk() || it->second->getCustomDiskSettings() != hashes[i])
                continue;
            if (!matches.empty() && restricted && it->second->customDiskUsesServerCredentials())
                continue;
            matches.emplace_back(i, std::move(name));
        }
    }
    if (matches.empty())
        matches.emplace_back(0, get_disk_name(hashes.front()));

    const auto disk_settings_hash = hashes[matches.front().first];
    const auto & disk_name = matches.front().second;
    if (!attach && (disk_name.empty() || disk_name == "." || disk_name == ".." || disk_name.contains('/')))
        throw Exception(ErrorCodes::BAD_ARGUMENTS, "Disk name cannot be empty, `.`, `..` or contain `/` ({})", disk_name);

    auto disk = context->getOrCreateDisk(
        disk_name,
        [&](const DisksMap & disks_map) -> DiskPtr
        {
            const auto wrapped = disks_map.find(config->getString(".disk", ""));
            const bool wraps_server_credentials = wrapped != disks_map.end() && wrapped->second->customDiskUsesServerCredentials();
            if (wraps_server_credentials && restricted)
                throw Exception(
                    ErrorCodes::ACCESS_DENIED,
                    "A dynamic disk created from user SQL may not wrap the disk `{}`, which uses the server's own credentials."
#if !CLICKHOUSE_CLOUD
                    " Enable the setting `s3_allow_server_credentials_in_user_queries` to allow it."
#endif
                    ,
                    wrapped->first);

            auto result = DiskFactory::instance().create(
                disk_name, *config, /* config_path */ "", context, disks_map, /* attach */ attach, /* custom_disk */ true);
            result->markDiskAsCustom(disk_settings_hash, uses_server_credentials || wraps_server_credentials);
            return result;
        });

    if (!disk->isCustomDisk())
        throw Exception(
            ErrorCodes::BAD_ARGUMENTS,
            "Disk `{}` already exists and is described by the config. It is impossible to redefine it.",
            disk_name);
    if (disk->getCustomDiskSettings() != disk_settings_hash && !attach)
        throw Exception(
            ErrorCodes::BAD_ARGUMENTS,
            "The disk `{}` is already configured as a custom disk in another table. It can't be redefined with different settings.",
            disk_name);
    if (restricted && disk->customDiskUsesServerCredentials())
        throw Exception(
            ErrorCodes::ACCESS_DENIED,
            "The disk `{}` uses the server's own credentials, which a dynamic disk created from user SQL may not use."
#if !CLICKHOUSE_CLOUD
            " Enable the setting `s3_allow_server_credentials_in_user_queries` to allow it."
#endif
            ,
            disk_name);

    /// Keep the release branch's local-disk boundary check when reusing a registered identity too.
    if (!attach && !disk->isRemote() && disk->getName() != "backup")
    {
        static constexpr auto custom_local_disks_base_dir_in_config = "custom_local_disks_base_directory";
        auto disk_path_expected_prefix = context->getConfigRef().getString(custom_local_disks_base_dir_in_config, "");
        if (disk_path_expected_prefix.empty())
            throw Exception(
                ErrorCodes::BAD_ARGUMENTS,
                "Base path for custom local disks must be defined in config file by `{}`",
                custom_local_disks_base_dir_in_config);
        if (!pathStartsWith(disk->getPath(), disk_path_expected_prefix))
            throw Exception(
                ErrorCodes::BAD_ARGUMENTS,
                "Path of the custom local disk must be inside `{}` directory",
                disk_path_expected_prefix);
    }
    return matches;
}

template <typename Matcher>
static void visitBottomUp(ASTPtr & ast, typename Matcher::Data & data)
{
    checkStackSize();
    for (auto & child : ast->children)
        visitBottomUp<Matcher>(child, data);
    Matcher::visit(ast, data);
}

static ASTs & getDiskFunctionArguments(const ASTPtr & ast)
{
    return assert_cast<ASTExpressionList &>(*ast->as<ASTFunction &>().arguments).children;
}

static ASTPtr makeCredentialsMarker(const std::string & marker)
{
    return makeASTFunction("equals", make_intrusive<ASTIdentifier>(marker), make_intrusive<ASTLiteral>(static_cast<UInt64>(1)));
}

struct CredentialsMarkersChange
{
    std::set<std::string> drop;
};

static void applyCredentialsMarkersChange(ASTs & args, const CredentialsMarkersChange & change)
{
    args.erase(
        std::remove_if(args.begin(), args.end(), [&](const ASTPtr & arg) { return change.drop.contains(getCredentialsMarker(arg)); }),
        args.end());
}

struct DiskConfigurationFlattenerData
{
    ContextPtr context;
    bool attach;
    bool for_system_database;
    bool stored_definition;
};

/// Retain child alternatives until their parent is matched: an old named wrapper hashes an old leaf's internal name.
struct DiskAlternative
{
    std::string name;
    std::vector<std::pair<ASTPtr, CredentialsMarkersChange>> edits;
};

struct FlattenedDisk
{
    ASTPtr * literal;
    std::vector<DiskAlternative> alternatives;
};

static std::vector<FlattenedDisk>
flattenDiskConfiguration(ASTPtr & flat, const ASTPtr & original, const DiskConfigurationFlattenerData & data)
{
    checkStackSize();
    std::vector<FlattenedDisk> below;
    for (size_t i = 0; i < flat->children.size(); ++i)
        std::ranges::move(flattenDiskConfiguration(flat->children[i], original->children[i], data), std::back_inserter(below));
    if (!isDiskFunction(flat))
        return below;

    const auto & args = getDiskFunctionArguments(flat);
    std::vector<CredentialsMarkersChange> changes{{}};
    if (!data.attach && !data.stored_definition && hasCredentialsMarker(args, S3_SERVER_CREDENTIALS_MARKER))
        changes.push_back({.drop = {S3_SERVER_CREDENTIALS_MARKER}});

    std::vector<std::vector<size_t>> combinations{{}};
    for (const auto & disk : below)
    {
        std::vector<std::vector<size_t>> extended;
        for (size_t alternative = 0; alternative < disk.alternatives.size(); ++alternative)
        {
            for (const auto & combination : combinations)
            {
                extended.push_back(combination);
                extended.back().push_back(alternative);
            }
        }
        combinations = std::move(extended);
    }

    auto set_disks_below = [&](const std::vector<size_t> & combination)
    {
        for (size_t i = 0; i < below.size(); ++i)
            *below[i].literal = make_intrusive<ASTLiteral>(below[i].alternatives[combination[i]].name);
    };

    std::vector<std::string> serializations;
    std::vector<DiskAlternative> forms;
    for (const auto & combination : combinations)
    {
        set_disks_below(combination);
        for (const auto & change : changes)
        {
            auto candidate = flat->clone();
            applyCredentialsMarkersChange(getDiskFunctionArguments(candidate), change);
            serializations.push_back(candidate->formatWithSecretsOneLine());
            DiskAlternative form{.name = {}, .edits = {}};
            if (!change.drop.empty())
                form.edits.emplace_back(original, change);
            for (size_t i = 0; i < below.size(); ++i)
                std::ranges::copy(below[i].alternatives[combination[i]].edits, std::back_inserter(form.edits));
            forms.push_back(std::move(form));
        }
    }
    set_disks_below(combinations.front());
    const auto matches = getOrCreateCustomDisk(args, serializations, data.context, data.attach, data.for_system_database);
    std::vector<DiskAlternative> alternatives;
    for (const auto & [index, name] : matches)
    {
        alternatives.push_back(std::move(forms[index]));
        alternatives.back().name = name;
    }
    flat = make_intrusive<ASTLiteral>(alternatives.front().name);
    return {FlattenedDisk{.literal = &flat, .alternatives = std::move(alternatives)}};
}

class ServerCredentialMarkerInjector
{
public:
    struct Data
    {
        ContextPtr context;
        bool for_system_database;
    };

    static void visit(ASTPtr & ast, Data & data)
    {
        if (!isDiskFunction(ast))
            return;
        auto * function = ast->as<ASTFunction>();
        auto * args_expr = function->arguments ? function->arguments->as<ASTExpressionList>() : nullptr;
        if (!args_expr)
            return;
        auto & args = args_expr->children;

        /// A fresh declaration cannot grant itself the persisted allowance.
        applyCredentialsMarkersChange(args, {.drop = {S3_SERVER_CREDENTIALS_MARKER}});
        for (const auto & arg : args)
        {
            const auto * eq = arg->as<ASTFunction>();
            if (eq && eq->name == "equals" && eq->arguments && eq->arguments->children.size() == 2
                && isDiskFunction(eq->arguments->children[1]))
                return;
        }
        DynamicS3DiskCredentialInfo info;
        getDiskConfigurationFromASTImpl(args, data.context, /* is_loading_from_existing_metadata */ false, &info, data.for_system_database);
        if (info.persist_server_credentials_allowance)
            args.push_back(makeCredentialsMarker(S3_SERVER_CREDENTIALS_MARKER));
    }
};

std::string DiskFromAST::createCustomDisk(
    const ASTPtr & disk_function_ast, ContextPtr context, bool attach, bool for_system_database, bool stored_definition)
{
    if (!isDiskFunction(disk_function_ast))
        throw Exception(ErrorCodes::BAD_ARGUMENTS, "Expected a disk function");
    if (!attach && !stored_definition)
    {
        ASTPtr to_mark = disk_function_ast;
        ServerCredentialMarkerInjector::Data inject_data{context, for_system_database};
        visitBottomUp<ServerCredentialMarkerInjector>(to_mark, inject_data);
    }
    auto ast = disk_function_ast->clone();
    const auto disks = flattenDiskConfiguration(ast, disk_function_ast, {context, attach, for_system_database, stored_definition});
    for (const auto & [disk_function, change] : disks.front().alternatives.front().edits)
        applyCredentialsMarkersChange(getDiskFunctionArguments(disk_function), change);
    return assert_cast<const ASTLiteral &>(*ast).value.safeGet<String>();
}

void DiskFromAST::ensureStoragePolicyIsAllowed(const std::string & storage_policy_name, ContextPtr context)
{
    if (!context->shouldRestrictUserQueryS3Credentials())
        return;
    const auto storage_policy = context->tryGetStoragePolicy(storage_policy_name);
    if (!storage_policy)
        return;
    for (const auto & disk : storage_policy->getDisks())
    {
        if (disk->customDiskUsesServerCredentials())
            throw Exception(
                ErrorCodes::ACCESS_DENIED,
                "The storage policy `{}` has the disk `{}`, which uses the server's own credentials and may not be used from user SQL."
#if !CLICKHOUSE_CLOUD
                " Enable the setting `s3_allow_server_credentials_in_user_queries` to allow it."
#endif
                ,
                storage_policy_name,
                disk->getName());
    }
}

void DiskFromAST::ensureDiskIsAllowed(const std::string & disk_name, ContextPtr context)
{
    if (!context->shouldRestrictUserQueryS3Credentials())
        return;
    if (context->getDisk(disk_name)->customDiskUsesServerCredentials())
        throw Exception(
            ErrorCodes::ACCESS_DENIED,
            "The disk `{}` uses the server's own credentials and may not be selected from user SQL."
#if !CLICKHOUSE_CLOUD
            " Enable the setting `s3_allow_server_credentials_in_user_queries` to allow it."
#endif
            ,
            disk_name);
}

void DiskFromAST::ensureDiskIsNotCustom(const std::string & disk_name, ContextPtr context)
{
    auto disk = context->getDisk(disk_name);
    if (disk->isCustomDisk())
        throw Exception(
            ErrorCodes::BAD_ARGUMENTS,
            "Disk name `{}` is a custom disk that is used in other table. "
            "That disk could not be used by a reference by other tables. The custom disk should be fully specified with a disk function.",
            disk_name);
}

}
