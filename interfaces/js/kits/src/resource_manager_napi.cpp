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

#include "resource_manager_addon.h"

#include <fstream>
#include <memory>
#include <vector>

#include "ability.h"
#include "foundation/aafwk/standard/frameworks/kits/appkit/native/ability_runtime/context/context.h"
#include "hilog/log.h"
#include "js_runtime_utils.h"
#include "node_api.h"

namespace OHOS {
namespace Global {
namespace Resource {
#define GET_PARAMS(env, info, num)    \
    size_t argc = num;                \
    napi_value argv[num] = {nullptr}; \
    napi_value thisVar = nullptr;     \
    void *data = nullptr;             \
    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data)

static constexpr OHOS::HiviewDFX::HiLogLabel LABEL = { LOG_CORE, 0xD001E00, "ResourceManagerJs" };
using namespace OHOS::HiviewDFX;
using namespace OHOS::AppExecFwk;

static void ExecuteGetResMgr(napi_env env, void* data)
{
    ResMgrAsyncContext *asyncContext = static_cast<ResMgrAsyncContext*>(data);

    asyncContext->createValueFunc_ = [](napi_env env, ResMgrAsyncContext &context) -> napi_value {
        napi_value result = ResourceManagerAddon::Create(env, context.bundleName_, context.resMgr_);
        if (result == nullptr) {
            context.SetErrorMsg("Failed to get ResourceManagerAddon");
            return nullptr;
        }
        return result;
    };
}

Ability* GetGlobalAbility(napi_env env)
{
    napi_value global;
    napi_status status = napi_get_global(env, &global);
    if (status != napi_ok) {
        HiLog::Error(LABEL, "Failed to get global");
        return nullptr;
    }

    napi_value abilityObj;
    status = napi_get_named_property(env, global, "ability", &abilityObj);
    if (status != napi_ok || abilityObj == nullptr) {
        HiLog::Warn(LABEL, "Failed to get ability property");
        return nullptr;
    }

    Ability* ability = nullptr;
    status = napi_get_value_external(env, abilityObj, (void **)&ability);
    if (status == napi_ok && ability != nullptr) {
        return ability;
    }

    return nullptr;
}

static bool InitAsyncContext(napi_env env, const std::string &bundleName, Ability* ability,
    const std::shared_ptr<AbilityRuntime::Context>& context, ResMgrAsyncContext &asyncContext)
{
    std::shared_ptr<ResourceManager> resMgr;
    if (ability != nullptr) {
        if (bundleName.empty()) {
            resMgr = ability->GetResourceManager();
        } else {
            std::shared_ptr<Context> bundleContext = ability->CreateBundleContext(bundleName, 0);
            if (bundleContext != nullptr) {
                resMgr = bundleContext->GetResourceManager();
            }
        }
    } else if (context != nullptr) {
        if (bundleName.empty()) {
            resMgr = context->GetResourceManager();
        } else {
            std::shared_ptr<OHOS::AbilityRuntime::Context> bundleContext = context->CreateBundleContext(bundleName);
            if (bundleContext != nullptr) {
                resMgr = bundleContext->GetResourceManager();
            }
        }
    }

    asyncContext.resMgr_ = resMgr;
    asyncContext.bundleName_ = bundleName;
    return resMgr != nullptr;
}

static napi_value GetResourceManager(napi_env env, napi_callback_info info)
{
    GET_PARAMS(env, info, 3);

    std::unique_ptr<ResMgrAsyncContext> asyncContext = std::make_unique<ResMgrAsyncContext>();
    std::shared_ptr<AbilityRuntime::Context> abilityRuntimeContext;
    std::string bundleName;
    for (size_t i = 0; i < argc; i++) {
        napi_valuetype valueType;
        napi_typeof(env, argv[i], &valueType);
        if (i == 0 && valueType == napi_object) {
            using WeakContextPtr = std::weak_ptr<AbilityRuntime::Context> *;
            WeakContextPtr objContext;
            napi_status status = napi_unwrap(env, argv[0], reinterpret_cast<void **>(&objContext));
            if (status != napi_ok || objContext == nullptr) {
                HiLog::Error(LABEL, "Failed to get objContext");
                return nullptr;
            }
            auto context = objContext->lock();
            if (context == nullptr) {
                HiLog::Error(LABEL, "Failed to get context");
                return nullptr;
            }
            abilityRuntimeContext = context;
        } else if ((i == 0 || i == 1) && valueType == napi_string) {
            size_t len = 0;
            napi_status status = napi_get_value_string_utf8(env, argv[i], nullptr, 0, &len);
            if (status != napi_ok) {
                HiLog::Error(LABEL, "Failed to get bundle name length");
                return nullptr;
            }
            std::vector<char> buf(len + 1);
            status = napi_get_value_string_utf8(env, argv[i], buf.data(), len + 1, &len);
            if (status != napi_ok) {
                HiLog::Error(LABEL, "Failed to get bundle name");
                return nullptr;
            }
            bundleName = buf.data();
        } else if ((i == 0 || i == 1 || i == 2) && valueType == napi_function) {
            napi_create_reference(env, argv[i], 1, &asyncContext->callbackRef_);
            break;
        } else {
            // self resourcemanager with promise
        }
    }

    napi_value result = nullptr;
    if (asyncContext->callbackRef_ == nullptr) {
        napi_create_promise(env, &asyncContext->deferred_, &result);
    } else {
        napi_get_undefined(env, &result);
    }

    if (!InitAsyncContext(env, bundleName, GetGlobalAbility(env), abilityRuntimeContext, *asyncContext)) {
        HiLog::Error(LABEL, "init async context failed");
        return nullptr;
    }

    napi_value resource = nullptr;
    napi_create_string_utf8(env, "getResourceManager", NAPI_AUTO_LENGTH, &resource);
    napi_status status = napi_create_async_work(env, nullptr, resource, ExecuteGetResMgr,
        ResMgrAsyncContext::Complete, static_cast<void*>(asyncContext.get()), &asyncContext->work_);
    if (status != napi_ok) {
        HiLog::Error(LABEL, "Failed to create async work for getResourceManager %{public}d", status);
        return result;
    }
    status = napi_queue_async_work(env, asyncContext->work_);
    if (status != napi_ok) {
        HiLog::Error(LABEL, "Failed to queue async work for getResourceManager %{public}d", status);
        return result;
    }

    asyncContext.release();
    return result;
}

static napi_value ResMgrInit(napi_env env, napi_value exports)
{
    napi_property_descriptor creatorProp[] = {
        DECLARE_NAPI_FUNCTION("getResourceManager", GetResourceManager),
    };
    napi_status status = napi_define_properties(env, exports, 1, creatorProp);
    if (status != napi_ok) {
        HiLog::Error(LABEL, "Failed to set getResourceManager at init");
        return nullptr;
    }
    return exports;
}

static napi_module g_resourceManagerModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = ResMgrInit,
    .nm_modname = "resourceManager",
    .nm_priv = ((void*)0),
    .reserved = {0}
};

extern "C" __attribute__((constructor)) void ResMgrRegister()
{
    napi_module_register(&g_resourceManagerModule);
}
} // namespace Resource
} // namespace Global
} // namespace OHOS