/*
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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

#ifndef RESOURCE_MANAGER_ADDON_H
#define RESOURCE_MANAGER_ADDON_H

#include <memory>
#include <string>

#include "napi/native_api.h"
#include "napi/native_node_api.h"
#include "resource_manager.h"

namespace OHOS {
namespace Global {
namespace Resource {
class ResourceManagerAddon {
public:
    static napi_value Create(
        napi_env env, const std::string& bundleName, const std::shared_ptr<ResourceManager>& resMgr);

    static bool Init(napi_env env);

    static void Destructor(napi_env env, void *nativeObject, void *finalize_hint);

    ResourceManagerAddon(const std::string& bundleName, const std::shared_ptr<ResourceManager>& resMgr);

    ~ResourceManagerAddon();

    inline std::shared_ptr<ResourceManager> GetResMgr()
    {
        return resMgr_;
    }

    std::string GetLocale(std::unique_ptr<ResConfig> &cfg);

private:
    static int GetResId(napi_env env, size_t argc, napi_value *argv);

    static napi_value ProcessOnlyIdParam(napi_env env, napi_callback_info info, const std::string &name,
        napi_async_execute_callback execute);

    static napi_value GetString(napi_env env, napi_callback_info info);

    static napi_value GetStringArray(napi_env env, napi_callback_info info);

    static napi_value GetMedia(napi_env env, napi_callback_info info);

    static napi_value GetMediaBase64(napi_env env, napi_callback_info info);

    static napi_value ProcessNoParam(napi_env env, napi_callback_info info, const std::string &name,
        napi_async_execute_callback execute);

    static napi_value GetConfiguration(napi_env env, napi_callback_info info);

    static napi_value GetDeviceCapability(napi_env env, napi_callback_info info);

    static napi_value GetPluralString(napi_env env, napi_callback_info info);

    static napi_value New(napi_env env, napi_callback_info info);

    static napi_value GetRawFile(napi_env env, napi_callback_info info);

    static std::string GetResNameOrPath(napi_env env, size_t argc, napi_value *argv);

    static napi_value GetRawFileDescriptor(napi_env env, napi_callback_info info);

    static napi_value CloseRawFileDescriptor(napi_env env, napi_callback_info info);

    static napi_value Release(napi_env env, napi_callback_info info);

    static std::string GetResName(napi_env env, size_t argc, napi_value *argv);

    static napi_value GetMediaByName(napi_env env, napi_callback_info info);

    static napi_value GetMediaBase64ByName(napi_env env, napi_callback_info info);

    static napi_value GetStringByName(napi_env env, napi_callback_info info);

    static napi_value GetStringArrayByName(napi_env env, napi_callback_info info);

    static napi_value GetPluralStringByName(napi_env env, napi_callback_info info);

    static napi_value ProcessIdNameParam(napi_env env, napi_callback_info info, const std::string& name,
        napi_async_execute_callback execute);

    static napi_value GetFloat(napi_env env, napi_callback_info info);

    static napi_value GetFloatByName(napi_env env, napi_callback_info info);

    std::string bundleName_;
    std::shared_ptr<ResourceManager> resMgr_;
};

struct ResMgrAsyncContext {
    napi_async_work work_;

    std::string bundleName_;
    int32_t resId_;
    int32_t param_;
    std::string path_;
    std::string resName_;
    float fValue_;

    typedef napi_value (*CreateNapiValue)(napi_env env, ResMgrAsyncContext &context);
    CreateNapiValue createValueFunc_;
    std::string value_;
    std::vector<std::string> arrayValue_;

    std::unique_ptr<char[]> mediaData;
    int len_;

    napi_deferred deferred_;
    napi_ref callbackRef_;

    std::string errMsg_;
    int success_;

    std::shared_ptr<ResourceManagerAddon> addon_;
    std::shared_ptr<ResourceManager> resMgr_;

    ResMgrAsyncContext() : work_(nullptr), resId_(0), param_(0), fValue_(0.0f), createValueFunc_(nullptr), len_(0),
        deferred_(nullptr), callbackRef_(nullptr), success_(true) {}

    void SetErrorMsg(const std::string &msg, bool withResId = false);

    static void Complete(napi_env env, napi_status status, void* data);
};
} // namespace Resource
} // namespace Global
} // namespace OHOS
#endif