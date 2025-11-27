#pragma once
#include "config.h"

#include <Disks/DiskObjectStorage/ObjectStorages/IObjectStorage.h>

namespace DB
{

struct Settings;

namespace GCSBlobStorage
{

struct RequestSettings
{
    RequestSettings() = default;

    bool read_only = false;
    int list_object_keys_size = 1000;
};

}
}
