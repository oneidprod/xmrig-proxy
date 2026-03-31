/* XMRig
 * Copyright (c) 2018-2021 SChernykh   <https://github.com/SChernykh>
 * Copyright (c) 2016-2021 XMRig       <https://github.com/xmrig>, <support@xmrig.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <cinttypes>
#include <iomanip>
#include <sstream>
#include <stdexcept>


#include "base/net/stratum/EquihashStratumClient.h"
#include "3rdparty/rapidjson/document.h"
#include "3rdparty/rapidjson/error/en.h"
#include "3rdparty/rapidjson/stringbuffer.h"
#include "3rdparty/rapidjson/writer.h"
#include "base/io/json/Json.h"
#include "base/io/json/JsonRequest.h"
#include "base/io/log/Log.h"
#include "base/kernel/interfaces/IClientListener.h"
#include "net/JobResult.h"


xmrig::EquihashStratumClient::EquihashStratumClient(int id, const char *agent, IClientListener *listener) :
    Client(id, agent, listener)
{
}


int64_t xmrig::EquihashStratumClient::submit(const JobResult &result)
{
#   ifndef XMRIG_PROXY_PROJECT
    if ((m_state != ConnectedState) || !m_authorized) {
        return -1;
    }
#   endif

    using namespace rapidjson;

    Document doc(kObjectType);
    auto &allocator = doc.GetAllocator();

    Value params(kArrayType);
    params.PushBack(m_user.toJSON(), allocator);
    params.PushBack(result.jobId.toJSON(), allocator);
    params.PushBack(Value(m_ntime.data(), allocator), allocator);

    // nonce2: worker-assigned nonce portion, padded to extraNonce2Size bytes
    {
        std::stringstream s;
        const size_t nonce2HexLen = m_extraNonce2Size * 2;
        if (result.nonce && strlen(result.nonce) > 0) {
            s << result.nonce;
        }
        std::string nonce2 = s.str();
        // pad or truncate to nonce2HexLen
        nonce2.resize(nonce2HexLen > 0 ? nonce2HexLen : 8, '0');
        params.PushBack(Value(nonce2.c_str(), allocator), allocator);
    }

    // solution: the Equihash solution from the miner (result.result field)
    if (result.result) {
        params.PushBack(Value(result.result, allocator), allocator);
    } else {
        params.PushBack(Value("", allocator), allocator);
    }

    JsonRequest::create(doc, m_sequence, "mining.submit", params);

#   ifdef XMRIG_PROXY_PROJECT
    m_results[m_sequence] = SubmitResult(m_sequence, result.diff, result.diff, result.id, 0);
#   else
    m_results[m_sequence] = SubmitResult(m_sequence, result.diff, result.diff, 0, result.backend);
#   endif

    return send(doc);
}


void xmrig::EquihashStratumClient::login()
{
    m_results.clear();

    subscribe();
    authorize();
}


void xmrig::EquihashStratumClient::onClose()
{
    m_authorized = false;
    Client::onClose();
}


bool xmrig::EquihashStratumClient::handleResponse(int64_t id, const rapidjson::Value &result, const rapidjson::Value &error)
{
    auto it = m_callbacks.find(id);
    if (it != m_callbacks.end()) {
        const uint64_t elapsed = Chrono::steadyMSecs() - it->second.ts;

        if (error.IsArray() || error.IsObject() || error.IsString()) {
            it->second.callback(error, false, elapsed);
        }
        else {
            it->second.callback(result, true, elapsed);
        }

        m_callbacks.erase(it);

        return true;
    }

    return handleSubmitResponse(id, errorMessage(error));
}


void xmrig::EquihashStratumClient::parseNotification(const char *method, const rapidjson::Value &params, const rapidjson::Value &)
{
    if (strcmp(method, "mining.set_difficulty") == 0) {
        if (!params.IsArray()) {
            LOG_ERR("%s " RED("invalid mining.set_difficulty notification: params is not an array"), tag());
            return;
        }

        auto arr = params.GetArray();

        if (arr.Empty()) {
            LOG_ERR("%s " RED("invalid mining.set_difficulty notification: params array is empty"), tag());
            return;
        }

        if (!arr[0].IsDouble() && !arr[0].IsUint64()) {
            LOG_ERR("%s " RED("invalid mining.set_difficulty notification: difficulty is not a number"), tag());
            return;
        }

        const double diff = arr[0].IsDouble() ? arr[0].GetDouble() : static_cast<double>(arr[0].GetUint64());
        // Zcash difficulty: store as-is; job.setDiff will use this value
        m_nextDifficulty = static_cast<uint64_t>(diff > 0 ? diff : 1);
    }

    if (strcmp(method, "mining.notify") == 0) {
        if (!params.IsArray()) {
            LOG_ERR("%s " RED("invalid mining.notify notification: params is not an array"), tag());
            return;
        }

        auto arr = params.GetArray();

        // Zcash notify: [jobid, version, prevhash, merkle_root, reserved, time, bits, clean_jobs]
        if (arr.Size() < 8) {
            LOG_ERR("%s " RED("invalid mining.notify notification: expected 8 fields for Equihash"), tag());
            return;
        }

        if (!arr[0].IsString()) {
            LOG_ERR("%s " RED("invalid mining.notify notification: invalid job id"), tag());
            return;
        }

        for (int i = 1; i <= 6; ++i) {
            if (!arr[i].IsString()) {
                LOG_ERR("%s " RED("invalid mining.notify notification: field %d is not a string"), tag(), i);
                return;
            }
        }

        auto algo = m_pool.algorithm();
        if (!algo.isValid()) {
            algo = m_pool.coin().algorithm();
        }

        Job job;
        job.setId(arr[0].GetString());
        job.setAlgorithm(algo);
        job.setExtraNonce(m_extraNonce.second);

        // Store ntime for later submit
        m_ntime = arr[5].GetString();

        // Build header blob for job:
        // version(4B) + prevhash(32B) + merkle_root(32B) + reserved(32B) + time(4B) + bits(4B) = 108 bytes = 216 hex
        // Pad to 140 bytes (280 hex) for nonce placeholder
        std::string blob;
        blob.reserve(280);
        blob += arr[1].GetString(); // version
        blob += arr[2].GetString(); // prevhash
        blob += arr[3].GetString(); // merkle_root
        blob += arr[4].GetString(); // reserved
        blob += arr[5].GetString(); // time
        blob += arr[6].GetString(); // bits

        // Pad to 280 hex chars (140 bytes) with zeros (nonce placeholder)
        blob.resize(280, '0');

        job.setBlob(blob.c_str());
        job.setDiff(m_nextDifficulty > 0 ? m_nextDifficulty : 1);

        bool ok = true;
        m_listener->onVerifyAlgorithm(this, algo, &ok);

        if (!ok) {
            if (!isQuiet()) {
                LOG_ERR("[%s] incompatible/disabled algorithm \"%s\" detected, reconnect", url(), algo.name());
            }
            close();
            return;
        }

        if (m_job != job) {
            m_job = std::move(job);

            if (!m_authorized) {
                m_authorized = true;
                m_listener->onLoginSuccess(this);
            }

            m_listener->onJobReceived(this, m_job, params);
        }
        else {
            if (!isQuiet()) {
                LOG_WARN("%s " YELLOW("duplicate job received, reconnect"), tag());
            }
            close();
        }
    }
}


void xmrig::EquihashStratumClient::setExtraNonce(const rapidjson::Value &nonce)
{
    if (!nonce.IsString()) {
        throw std::runtime_error("invalid mining.subscribe response: extra nonce is not a string");
    }

    const char *s = nonce.GetString();
    size_t len    = nonce.GetStringLength();

    // Skip "0x"
    if ((len >= 2) && (s[0] == '0') && (s[1] == 'x')) {
        s += 2;
        len -= 2;
    }

    if (len & 1) {
        throw std::runtime_error("invalid mining.subscribe response: extra nonce has an odd number of hex chars");
    }

    if (len > 8) {
        throw std::runtime_error("Invalid mining.subscribe response: extra nonce is too long");
    }

    std::string extra_nonce_str(s);
    extra_nonce_str.resize(16, '0');

    LOG_DEBUG("[%s] extra nonce set to %s", url(), s);

    m_extraNonce = { std::stoull(extra_nonce_str, nullptr, 16), s };
}


size_t xmrig::EquihashStratumClient::solutionSize() const
{
    const auto algo = m_pool.algorithm();

#   ifdef XMRIG_ALGO_EQUIHASH
    switch (algo.id()) {
    case Algorithm::EQUIHASH_144_5: return 100;
    case Algorithm::EQUIHASH_200_9:
    case Algorithm::EQUIHASH_210_9: return 1344;
    case Algorithm::EQUIHASH_192_7:
    default:                        return 400;
    }
#   endif

    return 400;
}


const char *xmrig::EquihashStratumClient::errorMessage(const rapidjson::Value &error)
{
    if (error.IsArray() && error.GetArray().Size() > 1) {
        auto &value = error.GetArray()[1];
        if (value.IsString()) {
            return value.GetString();
        }
    }

    if (error.IsString()) {
        return error.GetString();
    }

    if (error.IsObject()) {
        return Json::getString(error, "message");
    }

    return nullptr;
}


void xmrig::EquihashStratumClient::authorize()
{
    using namespace rapidjson;

    Document doc(kObjectType);
    auto &allocator = doc.GetAllocator();

    Value params(kArrayType);
    params.PushBack(m_user.toJSON(), allocator);
    params.PushBack(m_password.toJSON(), allocator);

    JsonRequest::create(doc, m_sequence, "mining.authorize", params);

    send(doc, [this](const rapidjson::Value &result, bool success, uint64_t elapsed) { onAuthorizeResponse(result, success, elapsed); });
}


void xmrig::EquihashStratumClient::onAuthorizeResponse(const rapidjson::Value &result, bool success, uint64_t)
{
    try {
        if (!success) {
            const auto message = errorMessage(result);
            if (message) {
                throw std::runtime_error(message);
            }

            throw std::runtime_error("mining.authorize call failed");
        }

        if (!result.IsBool()) {
            throw std::runtime_error("invalid mining.authorize response: result is not a boolean");
        }

        if (!result.GetBool()) {
            throw std::runtime_error("login failed");
        }
    } catch (const std::exception &ex) {
        LOG_ERR("%s " RED_BOLD("%s"), tag(), ex.what());

        close();
        return;
    }

    LOG_DEBUG("[%s] login succeeded", url());

    if (!m_authorized) {
        m_authorized = true;
        m_listener->onLoginSuccess(this);
    }
}


void xmrig::EquihashStratumClient::onSubscribeResponse(const rapidjson::Value &result, bool success, uint64_t)
{
    if (!success) {
        return;
    }

    try {
        if (!result.IsArray()) {
            throw std::runtime_error("invalid mining.subscribe response: result is not an array");
        }

        auto arr = result.GetArray();

        if (arr.Size() <= 1) {
            throw std::runtime_error("invalid mining.subscribe response: result array is too short");
        }

        setExtraNonce(arr[1]);

        if ((arr.Size() > 2) && (arr[2].IsUint())) {
            m_extraNonce2Size = arr[2].GetUint();
        }
    } catch (const std::exception &ex) {
        LOG_ERR("%s " RED("%s"), tag(), ex.what());

        m_extraNonce = { 0, {} };
    }
}


void xmrig::EquihashStratumClient::subscribe()
{
    using namespace rapidjson;

    Document doc(kObjectType);
    auto &allocator = doc.GetAllocator();

    Value params(kArrayType);
    params.PushBack(StringRef(agent()), allocator);

    JsonRequest::create(doc, m_sequence, "mining.subscribe", params);

    send(doc, [this](const rapidjson::Value &result, bool success, uint64_t elapsed) { onSubscribeResponse(result, success, elapsed); });
}
