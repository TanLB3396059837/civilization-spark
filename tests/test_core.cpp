#include "civ/crc/crc8.hpp"
#include "civ/crc/crc32.hpp"
#include "civ/codec/frame_v4.hpp"
#include "civ/channel/shm_channel.hpp"
#include "civ/quantum/quantum_rng.hpp"
#include "civ/core/vector.hpp"
#include <chrono>
#include <iostream>
#include <string>

using namespace civ;

static int g_passed = 0, g_failed = 0;

#define CHECK(cond, name) \
    do { \
        if (cond) { std::cout << "  [PASS] " << name << std::endl; ++g_passed; } \
        else { std::cout << "  [FAIL] " << name << std::endl; ++g_failed; } \
    } while (0)

int main() {
    std::cout << "========== KFL-M v3.3 核心测试 ==========" << std::endl;

    {
        std::vector<Byte> d{'1','2','3','4','5','6','7','8','9'};
        CHECK(crc8(d) == 0xF4, "CRC8 已知向量");
        CHECK(crc32(d) == 0xE3069283u, "CRC32C 已知向量");
    }

    {
        Byte buf[4];
        auto n = encode_index(5, 3, buf, 4);
        CHECK(n == 2, "内联索引长度");
        auto df = decode_frame(ByteView(buf, n));
        CHECK(df.session == 3 && df.index == 5, "内联索引值");
        CHECK(df.crc_ok, "内联 CRC");

        n = encode_index(200, 7, buf, 4);
        CHECK(n == 3, "8-bit 索引长度");
        df = decode_frame(ByteView(buf, n));
        CHECK(df.index == 200, "8-bit 索引值");

        n = encode_index(60000, 15, buf, 4);
        CHECK(n == 4, "16-bit 索引长度");
        df = decode_frame(ByteView(buf, n));
        CHECK(df.index == 60000 && df.session == 15, "16-bit 索引值");

        n = encode_hit(2, buf, 4);
        CHECK(n == 2, "命中帧长度");
        df = decode_frame(ByteView(buf, n));
        CHECK(df.is_hit, "命中标志");

        // 会话号只有 4 bit，超出部分必须被截断而不是污染类型位
        n = encode_index(1, 0x1F, buf, 4);
        df = decode_frame(ByteView(buf, n));
        CHECK(df.session == 15 && df.index == 1, "会话号截断");

        CHECK(encode_index(1, 0, nullptr, 4) == 0, "空输出缓冲");
    }

    {
        Byte buf[4];
        auto n = encode_index(100, 5, buf, 4);
        buf[1] ^= 0xFF;
        auto df = decode_frame(ByteView(buf, n));
        CHECK(!df.crc_ok, "CRC 检测损坏");
        CHECK(df.index == 0, "CRC 失败清零");
        CHECK(df.session == 0, "CRC 失败会话清零");
    }

    {
        QuantumRNG rng(42);
        auto a = rng.next_u64(), b = rng.next_u64();
        CHECK(a != b, "量子 RNG 输出不同");
        auto key = rng.generate_qkd_key();
        bool non_zero = false;
        for (auto k : key) if (k) non_zero = true;
        CHECK(non_zero, "QKD 密钥非零");

        bool range_ok = true;
        bool float_ok = true;
        for (int i = 0; i < 10000; ++i) {
            auto r = rng.next_range(5, 9);
            if (r < 5 || r >= 9) range_ok = false;
            float f = rng.next_float();
            if (!(f >= 0.0f && f < 1.0f)) float_ok = false;
        }
        CHECK(range_ok, "RNG 范围边界");
        CHECK(float_ok, "RNG 浮点范围");
    }

    {
        // 共享内存单向回环
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch().count();
#ifdef _WIN32
        std::string name = "Local\\civ_shm_test_" + std::to_string(stamp);
#else
        std::string name = "/civ_shm_test_" + std::to_string(stamp);
#endif
        ShmChannel server(name, true, 4096);
        ShmChannel client(name, false, 4096);
        std::vector<Byte> payload{1, 2, 3, 4, 5, 0, 255};
        client.send(payload);
        auto received = server.recv();
        CHECK(received == payload, "共享内存收发");
    }

    {
        Vector a, b;
        a[0] = 1.0f;
        b[0] = 3.0f;
        CHECK(std::abs(a.l1_error(b) - 2.0f / DIM) < 1e-6f,
              "向量 L1 误差");
    }

    std::cout << "\n结果：" << g_passed << " 通过, "
              << g_failed << " 失败" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
