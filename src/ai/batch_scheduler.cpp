#include "civ/ai/batch_scheduler.hpp"
#include "civ/ai/shared_model.hpp"
#include <iostream>
#include <algorithm>

namespace civ {

int BatchScheduler::g_decode_calls = 0;
int BatchScheduler::g_total_tokens = 0;

bool BatchScheduler::step(std::vector<LLMAgent*>& active) {
    auto& pool = SharedModelPool::instance();
    if (!pool.is_loaded()) return false;

    std::vector<DecodeRequest> reqs;
    std::vector<LLMAgent*> to_sample;
    reqs.reserve(active.size());
    to_sample.reserve(active.size());

    for (auto* a : active) {
        std::int32_t next = a->sample_next_token();
        if (next < 0) continue;

        // 以 KV cache 的实际位置为准，修正本地 n_past 漂移。
        const int kv_pos =
            pool.sequence_kv_usage(a->state().seq_id);
        if (kv_pos != a->state().n_past) a->state().n_past = kv_pos;

        DecodeRequest r;
        r.seq_id = a->state().seq_id;
        r.token  = next;
        r.pos    = a->state().n_past++;
        r.want_logits = true;

        reqs.push_back(r);
        to_sample.push_back(a);
    }

    if (reqs.empty()) return false;

    std::vector<float> logits;
    if (!pool.batch_decode(reqs, logits)) return false;

    ++g_decode_calls;
    g_total_tokens += static_cast<int>(reqs.size());

    int n_vocab = pool.n_vocab();
    for (std::size_t i = 0; i < to_sample.size(); ++i) {
        std::vector<float> slice(
            logits.begin() + static_cast<std::ptrdiff_t>(i * n_vocab),
            logits.begin() + static_cast<std::ptrdiff_t>((i + 1) * n_vocab));
        to_sample[i]->set_logits(std::move(slice));
    }
    return true;
}

std::vector<GenResult> BatchScheduler::generate_parallel(
    std::vector<LLMAgent*> agents,
    const std::vector<std::string>& prompts,
    int max_tokens,
    int n_ctx) {

    std::vector<GenResult> results(agents.size());

    if (agents.size() != prompts.size()) {
        for (auto& r : results) {
            r.ok = false;
            r.error = "agents.size() != prompts.size()";
        }
        return results;
    }

    auto& pool = SharedModelPool::instance();
    if (!pool.is_loaded()) {
        for (std::size_t i = 0; i < agents.size(); ++i) {
            results[i].text = agents[i]->generate(prompts[i], max_tokens);
        }
        return results;
    }

    std::vector<LLMAgent*> active;
    active.reserve(agents.size());
    for (std::size_t i = 0; i < agents.size(); ++i) {
        agents[i]->begin_generation(prompts[i], max_tokens, n_ctx);
        if (agents[i]->is_generating()) {
            active.push_back(agents[i]);
        } else {
            results[i].ok = false;
            results[i].error = "begin_generation failed";
        }
    }

    // 只遍历活跃 Agent
    int max_iter = max_tokens + 8;
    for (int iter = 0; iter < max_iter; ++iter) {
        active.erase(
            std::remove_if(active.begin(), active.end(),
                [](LLMAgent* a) { return !a->is_generating(); }),
            active.end());

        if (active.empty()) break;
        if (!step(active)) break;
    }

    for (std::size_t i = 0; i < agents.size(); ++i) {
        if (results[i].ok)
            results[i].text = agents[i]->take_result();
    }

    return results;
}

} // namespace civ
