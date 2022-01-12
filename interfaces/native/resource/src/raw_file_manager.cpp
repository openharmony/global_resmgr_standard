/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "raw_file_manager.h"

#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include "raw_dir.h"
#include "raw_file.h"
#include "resource_manager.h"
#include "resource_manager_addon.h"
#include "resource_manager_impl.h"
#include "hilog/log.h"

using namespace OHOS::Global::Resource;
using namespace OHOS::HiviewDFX;

namespace {
    constexpr HiLogLabel LABEL = {LOG_CORE, 0xD001E00, "RawFile"};
}

struct NativeResourceManager {
    std::shared_ptr<ResourceManager> resManager = nullptr;
};

struct FileNameCache {
    int maxCount = 0;
    std::vector<std::string> names;
};

struct RawDir {
    std::shared_ptr<ResourceManager> resManager = nullptr;
    struct FileNameCache fileNameCache;
};

struct RawFile {
    const std::string filePath;
    long length;
    FILE* pf;

    explicit RawFile(const std::string &path) : filePath(path), length(0L), pf(nullptr) {}

    ~RawFile()
    {
        if (pf != nullptr) {
            fclose(pf);
            pf = nullptr;
        }
    }

    bool open()
    {
        pf = std::fopen(filePath.c_str(), "rb");
        return pf != nullptr;
    }
};

NativeResourceManager *InitNativeResourceManager(napi_env env, napi_value jsResMgr)
{
    napi_valuetype valueType;
    napi_typeof(env, jsResMgr, &valueType);
    if (valueType != napi_object) {
        HiLog::Error(LABEL, "jsResMgr is not an object");
        return nullptr;
    }
    std::shared_ptr<ResourceManagerAddon> *addonPtr = nullptr;
    napi_status status = napi_unwrap(env, jsResMgr, reinterpret_cast<void **>(&addonPtr));
    if (status != napi_ok) {
        HiLog::Error(LABEL, "Failed to get native resourcemanager");
        return nullptr;
    }
    std::unique_ptr<NativeResourceManager> result = std::make_unique<NativeResourceManager>();
    result->resManager = (*addonPtr)->GetResMgr();
    return result.release();
}

void ReleaseNativeResourceManager(NativeResourceManager *resMgr)
{
    if (resMgr != nullptr) {
        delete resMgr;
    }
}

RawDir *OpenRawDir(const NativeResourceManager *mgr, const char *dirName)
{
    if (mgr == nullptr || dirName == nullptr) {
        return nullptr;
    }
    ResourceManagerImpl* impl = static_cast<ResourceManagerImpl *>(mgr->resManager.get());
    std::string tempName = dirName;
    const std::string rawFileDirName = "rawfile/";
    if (tempName.length() < rawFileDirName.length()
        || (tempName.compare(0, rawFileDirName.length(), rawFileDirName) != 0)) {
        tempName = rawFileDirName + tempName;
    }
    std::unique_ptr<RawDir> result = std::make_unique<RawDir>();
    std::vector<std::string> resourcesPaths = impl->GetResourcePaths();
    for (auto iter = resourcesPaths.begin(); iter != resourcesPaths.end(); iter++) {
        std::string currentPath = *iter + tempName;
        DIR* dir = opendir(currentPath.c_str());
        if (dir == nullptr) {
            continue;
        }
        struct dirent *dirp = readdir(dir);
        while (dirp != nullptr) {
            if (std::strcmp(dirp->d_name, ".") == 0 ||
                std::strcmp(dirp->d_name, "..") == 0) {
                dirp = readdir(dir);
                continue;
            }
            if (dirp->d_type == DT_REG) {
                result->fileNameCache.names.push_back(tempName + "/" + dirp->d_name);
            }

            dirp = readdir(dir);
        }
        closedir(dir);
    }
    return result.release();
}

RawFile *OpenRawFile(const NativeResourceManager *mgr, const char *fileName)
{
    if (mgr == nullptr || fileName == nullptr) {
        return nullptr;
    }

    std::string filePath;
    RState state = mgr->resManager->GetRawFilePathByName(fileName, filePath);
    if (state != SUCCESS) {
        return nullptr;
    }
    std::unique_ptr<RawFile> result = std::make_unique<RawFile>(filePath);
    if (!result->open()) {
        return nullptr;
    }

    std::fseek(result->pf, 0, SEEK_END);
    result->length = ftell(result->pf);
    std::fseek(result->pf, 0, SEEK_SET);
    return result.release();
}

int GetRawFileCount(RawDir *rawDir)
{
    if (rawDir == nullptr) {
        return 0;
    }
    return rawDir->fileNameCache.names.size();
}

const char *GetRawFileName(RawDir *rawDir, int index)
{
    if (rawDir == nullptr || index < 0) {
        return nullptr;
    }
    int rawFileCount = rawDir->fileNameCache.names.size();
    if (rawFileCount == 0 || index >= rawFileCount) {
        return nullptr;
    }
    return rawDir->fileNameCache.names[index].c_str();
}

void CloseRawDir(RawDir *rawDir)
{
    if (rawDir != nullptr) {
        delete rawDir;
    }
}

int ReadRawFile(const RawFile *rawFile, void *buf, int length)
{
    if (rawFile == nullptr || buf == nullptr || length == 0) {
        return 0;
    }
    return std::fread(buf, 1, length, rawFile->pf);
}

long GetRawFileSize(RawFile *rawFile)
{
    if (rawFile == nullptr) {
        return 0;
    }

    return rawFile->length;
}

void CloseRawFile(RawFile *rawFile)
{
    if (rawFile != nullptr) {
        delete rawFile;
    }
}

bool GetRawFileDescriptor(const RawFile *rawFile, RawFileDescriptor &descriptor)
{
    if (rawFile == nullptr) {
        return false;
    }
    int fd = open(rawFile->filePath.c_str(), O_RDONLY);
    if (fd > 0) {
        descriptor.fd = fd;
        descriptor.length = rawFile->length;
    } else {
        return false;
    }
    return true;
}

bool ReleaseRawFileDescriptor(const RawFileDescriptor &descriptor)
{
    if (descriptor.fd > 0) {
        return close(descriptor.fd) == 0;
    }
    return true;
}
