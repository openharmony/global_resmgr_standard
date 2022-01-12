/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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
#include "hap_manager.h"

#include <algorithm>
#include <fstream>
#include <ohos/init_data.h>

#include "auto_mutex.h"
#include "hilog_wrapper.h"
#include "locale_matcher.h"

#ifdef __WINNT__
#include <shlwapi.h>
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace OHOS {
namespace Global {
namespace Resource {
constexpr uint32_t PLURAL_CACHE_MAX_COUNT = 3;

HapManager::HapManager(ResConfigImpl *resConfig)
    : resConfig_(resConfig)
{
}

bool HapManager::icuInitialized = HapManager::Init();

bool HapManager::Init()
{
#ifdef __IDE_PREVIEW__
#ifdef __WINNT__
    MEMORY_BASIC_INFORMATION mbi;
    if (::VirtualQuery((LPCVOID)SetHwIcuDirectory, &mbi, sizeof(mbi)) != 0) {
        char path[MAX_PATH] = { 0 };
        GetModuleFileName((HMODULE)mbi.AllocationBase, path, MAX_PATH);
        std::string tempPath(path);
        auto pos = tempPath.rfind('\\');
        if (pos != std::string::npos) {
            u_setDataDirectory(tempPath.substr(0, pos).c_str());
        }
    }
#else
    Dl_info info;
    if (dladdr((void*)SetHwIcuDirectory, &info) != 0) {
        std::string tempPath(info.dli_fname);
        auto pos = tempPath.rfind('/');
        if (pos != std::string::npos) {
            u_setDataDirectory(tempPath.substr(0, pos).c_str());
        }
    }
#endif
#else
    SetHwIcuDirectory();
#endif

    return true;
}

std::string HapManager::GetPluralRulesAndSelect(int quantity)
{
    AutoMutex mutex(this->lock_);
    std::string defaultRet("other");
    if (this->resConfig_ == nullptr || this->resConfig_->GetResLocale() == nullptr ||
        this->resConfig_->GetResLocale()->GetLanguage() == nullptr) {
        HILOG_ERROR("GetPluralRules language is null!");
        return defaultRet;
    }
    std::string language = this->resConfig_->GetResLocale()->GetLanguage();

    icu::PluralRules *pluralRules = nullptr;
    for (uint32_t i = 0; i < plurRulesCache_.size(); i++) {
        auto pair = plurRulesCache_[i];
        if (language == pair.first) {
            // cache hit
            pluralRules = pair.second;
            break;
        }
    }

    if (pluralRules == nullptr) {
        // no cache hit
        icu::Locale locale(language.c_str());
        if (locale.isBogus()) {
            HILOG_ERROR("icu::Locale init error : %s", language.c_str());
            return defaultRet;
        }
        UErrorCode status = U_ZERO_ERROR;
        pluralRules = icu::PluralRules::forLocale(locale, status);
        if (status != U_ZERO_ERROR) {
            HILOG_ERROR("icu::PluralRules::forLocale error : %d", status);
            return defaultRet;
        }
        // after PluralRules created, we add it to cache, if > 3 delete oldest one
        if (plurRulesCache_.size() >= PLURAL_CACHE_MAX_COUNT) {
            HILOG_DEBUG("cache rotate delete plurRulesMap_ %s", plurRulesCache_[0].first.c_str());
            delete (plurRulesCache_[0].second);
            plurRulesCache_.erase(plurRulesCache_.begin());
        }
        auto plPair = std::make_pair(language, pluralRules);
        plurRulesCache_.push_back(plPair);
    }
    std::string converted;
    icu::UnicodeString us = pluralRules->select(quantity);
    us.toUTF8String(converted);
    return converted;
}

const IdItem *HapManager::FindResourceById(uint32_t id)
{
    auto qualifierValue = FindQualifierValueById(id);
    if (qualifierValue == nullptr) {
        return nullptr;
    }
    return qualifierValue->GetIdItem();
}

const IdItem *HapManager::FindResourceByName(const char *name, const ResType resType)
{
    auto qualifierValue = FindQualifierValueByName(name, resType);
    if (qualifierValue == nullptr) {
        return nullptr;
    }
    return qualifierValue->GetIdItem();
}

const HapResource::ValueUnderQualifierDir *HapManager::FindQualifierValueByName(
    const char *name, const ResType resType)
{
    const HapResource::IdValues *idValues = this->GetResourceListByName(name, resType);
    if (idValues == nullptr) {
        return nullptr;
    }
    const std::vector<HapResource::ValueUnderQualifierDir *> paths = idValues->GetLimitPathsConst();

    size_t len = paths.size();
    size_t i = 0;
    size_t bestIndex = -1;
    const ResConfigImpl *bestResConfig = nullptr;
    const ResConfigImpl *currentResConfig = this->resConfig_;
    for (i = 0; i < len; i++) {
        HapResource::ValueUnderQualifierDir *path = paths[i];
        const ResConfigImpl *resConfig = path->GetResConfig();
        if (this->resConfig_->Match(resConfig)) {
            if (bestResConfig == nullptr) {
                bestIndex = i;
                bestResConfig = resConfig;
            } else if (bestResConfig->IsMoreSuitable(resConfig, currentResConfig)) {
                continue;
            } else {
                bestResConfig = resConfig;
                bestIndex = i;
            }
        }
    }
    return paths[bestIndex];
}

const HapResource::ValueUnderQualifierDir *HapManager::FindQualifierValueById(uint32_t id)
{
    const HapResource::IdValues *idValues = this->GetResourceList(id);
    if (idValues == nullptr) {
        return nullptr;
    }
    const std::vector<HapResource::ValueUnderQualifierDir *> paths = idValues->GetLimitPathsConst();

    size_t len = paths.size();
    size_t i = 0;
    size_t bestIndex = -1;
    const ResConfigImpl *bestResConfig = nullptr;
    const ResConfigImpl *currentResConfig = this->resConfig_;
    for (i = 0; i < len; i++) {
        HapResource::ValueUnderQualifierDir *path = paths[i];
        const ResConfigImpl *resConfig = path->GetResConfig();
        if (this->resConfig_->Match(resConfig)) {
            if (bestResConfig == nullptr) {
                bestIndex = i;
                bestResConfig = resConfig;
            } else {
                if (bestResConfig->IsMoreSuitable(resConfig, currentResConfig)) {
                    continue;
                } else {
                    bestResConfig = resConfig;
                    bestIndex = i;
                }
            }
        }
    }
    return paths[bestIndex];
}

RState HapManager::FindRawFile(const std::string &name, std::string &outValue)
{
#ifdef __WINNT__
    char seperator = '\\';
#else
    char seperator = '/';
#endif
    for (auto iter = hapResources_.rbegin(); iter != hapResources_.rend(); iter++) {
        std::string indexPath = (*iter)->GetIndexPath();
        auto index = indexPath.rfind(seperator);
        if (index == std::string::npos) {
            HILOG_ERROR("index path format error, %s", indexPath.c_str());
            continue;
        }
        std::string resourcesIndexPath = indexPath.substr(0, index);
        char tmpPath[PATH_MAX] = {0};
        std::string tempName = name;
        const std::string rawFileDirName = "rawfile/";
        if (tempName.length() <= rawFileDirName.length()
            || (tempName.compare(0, rawFileDirName.length(), rawFileDirName) != 0)) {
            tempName = rawFileDirName + tempName;
        }
#ifdef __WINNT__
        if (!PathCanonicalizeA(tmpPath, (resourcesIndexPath + "/resources/" + tempName).c_str())) {
            continue;
        }
#else
        if (realpath((resourcesIndexPath + "/resources/" + tempName).c_str(), tmpPath) == nullptr) {
            HILOG_ERROR("FindRawFile path to realpath error");
            continue;
        }
#endif
        const std::string realPath = tmpPath;
        if (realPath.length() > resourcesIndexPath.length()
            && (realPath.compare(0, resourcesIndexPath.length(), resourcesIndexPath) == 0)) {
            std::fstream inputFile;
            inputFile.open(realPath, std::ios::in);
            if (inputFile) {
                outValue = realPath;
                return SUCCESS;
            }
        }
    }
    return RState::NOT_FOUND;
}

RState HapManager::UpdateResConfig(ResConfig &resConfig)
{
    AutoMutex mutex(this->lock_);
    this->resConfig_->Copy(resConfig);
    RState rState = this->ReloadAll();
    if (rState != SUCCESS) {
        HILOG_ERROR("ReloadAll() failed when UpdateResConfig!");
    }
    return rState;
}


void HapManager::GetResConfig(ResConfig &resConfig)
{
    AutoMutex mutex(this->lock_);
    resConfig.Copy(*(this->resConfig_));
}

bool HapManager::AddResource(const char *path)
{
    AutoMutex mutex(this->lock_);
    return this->AddResourcePath(path);
}

HapManager::~HapManager()
{
    for (size_t i = 0; i < hapResources_.size(); ++i) {
        auto ptr = hapResources_[i];
        delete (ptr);
    }
    delete resConfig_;

    auto iter = plurRulesCache_.begin();
    for (; iter != plurRulesCache_.end(); iter++) {
        HILOG_DEBUG("delete plurRulesMap_ %s", iter->first.c_str());
        auto ptr = iter->second;
        delete (ptr);
    }
}

const HapResource::IdValues *HapManager::GetResourceList(uint32_t ident) const
{
    // one id only exit in one hap
    for (size_t i = 0; i < hapResources_.size(); ++i) {
        HapResource *pResource = hapResources_[i];
        const HapResource::IdValues *out = pResource->GetIdValues(ident);
        if (out != nullptr) {
            return out;
        }
    }
    return nullptr;
}

const HapResource::IdValues *HapManager::GetResourceListByName(const char *name, const ResType resType) const
{
    // first match will return
    for (size_t i = 0; i < hapResources_.size(); ++i) {
        HapResource *pResource = hapResources_[i];
        const HapResource::IdValues *out = pResource->GetIdValuesByName(std::string(name), resType);
        if (out != nullptr) {
            return out;
        }
    }
    return nullptr;
}

bool HapManager::AddResourcePath(const char *path)
{
    std::string sPath(path);
    std::vector<std::string>::iterator it = std::find(loadedHapPaths_.begin(),
        loadedHapPaths_.end(), sPath);
    if (it != loadedHapPaths_.end()) {
        HILOG_ERROR(" %s has already been loaded!", path);
        return false;
    }
    const HapResource *pResource = HapResource::LoadFromIndex(path, resConfig_);
    if (pResource == nullptr) {
        return false;
    }
    this->hapResources_.push_back((HapResource *)pResource);
    this->loadedHapPaths_.push_back(sPath);
    return true;
}

RState HapManager::ReloadAll()
{
    if (hapResources_.size() == 0) {
        return SUCCESS;
    }
    std::vector<HapResource *> newResources;
    for (size_t i = 0; i < hapResources_.size(); ++i) {
        const HapResource *pResource = HapResource::LoadFromIndex(hapResources_[i]->GetIndexPath().c_str(), resConfig_);
        if (pResource == nullptr) {
            for (size_t i = 0; i < newResources.size(); ++i) {
                delete (newResources[i]);
            }
            return HAP_INIT_FAILED;
        }
        newResources.push_back((HapResource *)pResource);
    }
    for (size_t i = 0; i < hapResources_.size(); ++i) {
        delete (hapResources_[i]);
    }
    hapResources_ = newResources;
    return SUCCESS;
}

std::vector<std::string> HapManager::GetResourcePaths()
{
    std::vector<std::string> result;
    for (auto iter = hapResources_.rbegin(); iter != hapResources_.rend(); iter++) {
        std::string indexPath = (*iter)->GetIndexPath();
        auto index = indexPath.rfind('/');
        if (index == std::string::npos) {
            HILOG_ERROR("index path format error, %s", indexPath.c_str());
            continue;
        }

        result.emplace_back(indexPath.substr(0, index) + "/resources/");
    }

    return result;
}
} // namespace Resource
} // namespace Global
} // namespace OHOS