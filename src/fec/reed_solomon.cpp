#include "fec/reed_solomon.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace ed::fec {

namespace {

constexpr uint16_t kPrimPoly = 0x187;  // x^8 + x^7 + x^2 + x + 1
constexpr int kFieldOrder = 255;
constexpr int kNumRoots = 32;
constexpr int kFirstRootExponent = 212;  // (11 * 112) mod 255
constexpr int kRootGap = 11;
constexpr int kMaxCorrectable = 16;
constexpr int kBlock = 255;
constexpr int kMsg = 223;

struct Gf {
    uint8_t exp[512];
    uint8_t log[256];
    Gf() {
        int x = 1;
        for (int i = 0; i < kFieldOrder; ++i) {
            exp[i] = static_cast<uint8_t>(x);
            log[x] = static_cast<uint8_t>(i);
            x = x << 1;
            if ((x & 0x100) != 0) {
                x ^= kPrimPoly;
            }
        }
        for (int i = kFieldOrder; i < 512; ++i) {
            exp[i] = exp[i - kFieldOrder];
        }
    }
};

const Gf& gf() {
    static const Gf instance;
    return instance;
}

uint8_t gmul(uint8_t a, uint8_t b) {
    if (a == 0 || b == 0) {
        return 0;
    }
    const Gf& f = gf();
    return f.exp[(f.log[a] + f.log[b]) % kFieldOrder];
}

uint8_t gpow(uint8_t a, int n) {
    if (a == 0) {
        return 0;
    }
    const Gf& f = gf();
    int e = (static_cast<int>(f.log[a]) * (n % kFieldOrder)) % kFieldOrder;
    if (e < 0) {
        e += kFieldOrder;
    }
    return f.exp[e];
}

uint8_t ginv(uint8_t a) {
    const Gf& f = gf();
    return f.exp[(kFieldOrder - f.log[a]) % kFieldOrder];
}

const std::array<uint8_t, kNumRoots>& roots() {
    static const std::array<uint8_t, kNumRoots> value = [] {
        std::array<uint8_t, kNumRoots> r{};
        const Gf& f = gf();
        for (int j = 0; j < kNumRoots; ++j) {
            r[j] = f.exp[(kFirstRootExponent + kRootGap * j) % kFieldOrder];
        }
        return r;
    }();
    return value;
}

// Solve A x = b over GF(256); A is n x n. Returns false if singular.
bool solve_gf(std::vector<std::vector<uint8_t>>& a, std::vector<uint8_t>& b,
              std::vector<uint8_t>& x) {
    const int n = static_cast<int>(b.size());
    for (int col = 0; col < n; ++col) {
        int pivot = -1;
        for (int r = col; r < n; ++r) {
            if (a[r][col] != 0) {
                pivot = r;
                break;
            }
        }
        if (pivot < 0) {
            return false;
        }
        std::swap(a[col], a[pivot]);
        std::swap(b[col], b[pivot]);
        const uint8_t inv = ginv(a[col][col]);
        for (int c = col; c < n; ++c) {
            a[col][c] = gmul(a[col][c], inv);
        }
        b[col] = gmul(b[col], inv);
        for (int r = 0; r < n; ++r) {
            if (r == col || a[r][col] == 0) {
                continue;
            }
            const uint8_t factor = a[r][col];
            for (int c = col; c < n; ++c) {
                a[r][c] ^= gmul(factor, a[col][c]);
            }
            b[r] ^= gmul(factor, b[col]);
        }
    }
    x = b;
    return true;
}

struct DualTables {
    uint8_t to_dual[256];
    uint8_t from_dual[256];
    DualTables() {
        const Gf& f = gf();
        for (int a = 0; a < 256; ++a) {
            uint8_t xval = 0;
            for (int i = 0; i < 8; ++i) {
                if (((a >> i) & 1) != 0) {
                    xval ^= f.exp[i];
                }
            }
            uint8_t d = 0;
            for (int j = 0; j < 8; ++j) {
                const uint8_t prod = gmul(xval, f.exp[j]);
                uint8_t t = prod;
                uint8_t tr = 0;
                for (int k = 0; k < 8; ++k) {
                    tr ^= t;
                    t = gmul(t, t);
                }
                if ((tr & 1) != 0) {
                    d = static_cast<uint8_t>(d | (1u << j));
                }
            }
            to_dual[a] = d;
        }
        for (int a = 0; a < 256; ++a) {
            from_dual[to_dual[a]] = static_cast<uint8_t>(a);
        }
    }
};

const DualTables& dual_tables() {
    static const DualTables instance;
    return instance;
}

}  // namespace

std::vector<uint8_t> rs_generator_ascending() {
    std::vector<uint8_t> g{1};
    for (uint8_t root : roots()) {
        std::vector<uint8_t> ng(g.size() + 1, 0);
        for (std::size_t m = 0; m < g.size(); ++m) {
            ng[m] ^= gmul(g[m], root);
            ng[m + 1] ^= g[m];
        }
        g.swap(ng);
    }
    return g;
}

void rs_dual_basis_tables(uint8_t* to_dual, uint8_t* from_dual) {
    const DualTables& t = dual_tables();
    if (to_dual != nullptr) {
        std::memcpy(to_dual, t.to_dual, 256);
    }
    if (from_dual != nullptr) {
        std::memcpy(from_dual, t.from_dual, 256);
    }
}

void ReedSolomon::configure(bool dual_basis) { dual_basis_ = dual_basis; }

void ReedSolomon::encode_block(uint8_t* block) const {
    if (block == nullptr) {
        return;
    }
    uint8_t buf[kBlock];
    std::memcpy(buf, block, kBlock);
    if (dual_basis_) {
        const DualTables& t = dual_tables();
        for (int i = 0; i < kBlock; ++i) {
            buf[i] = t.from_dual[buf[i]];
        }
    }

    // Generator in descending order: gd[k] = coefficient of x^(32-k).
    const std::vector<uint8_t> g = rs_generator_ascending();
    uint8_t gd[33];
    for (int k = 0; k <= 32; ++k) {
        gd[k] = g[static_cast<std::size_t>(32 - k)];
    }

    // Polynomial long division of (message * x^32) by g(x).
    uint8_t rem[kBlock];
    std::memcpy(rem, buf, kBlock);
    for (int i = kMsg; i < kBlock; ++i) {
        rem[i] = 0;
    }
    for (int i = 0; i < kMsg; ++i) {
        const uint8_t coef = rem[i];
        if (coef == 0) {
            continue;
        }
        for (int k = 0; k <= 32; ++k) {
            rem[i + k] ^= gmul(gd[k], coef);
        }
    }
    for (int i = kMsg; i < kBlock; ++i) {
        buf[i] = rem[i];
    }

    if (dual_basis_) {
        const DualTables& t = dual_tables();
        for (int i = 0; i < kBlock; ++i) {
            buf[i] = t.to_dual[buf[i]];
        }
    }
    std::memcpy(block, buf, kBlock);
}

int ReedSolomon::decode_block(uint8_t* block) const {
    if (block == nullptr) {
        return -1;
    }
    uint8_t buf[kBlock];
    std::memcpy(buf, block, kBlock);
    if (dual_basis_) {
        const DualTables& t = dual_tables();
        for (int i = 0; i < kBlock; ++i) {
            buf[i] = t.from_dual[buf[i]];
        }
    }

    // Syndromes: S[j] = sum_i buf[i] * root_j^(254-i).
    uint8_t s[kNumRoots];
    bool all_zero = true;
    for (int j = 0; j < kNumRoots; ++j) {
        uint8_t acc = 0;
        const uint8_t root = roots()[static_cast<std::size_t>(j)];
        for (int i = 0; i < kBlock; ++i) {
            acc ^= gmul(buf[i], gpow(root, 254 - i));
        }
        s[j] = acc;
        if (acc != 0) {
            all_zero = false;
        }
    }
    if (all_zero) {
        return 0;
    }

    // Berlekamp-Massey error locator (ascending; c[0] = 1).
    std::vector<uint8_t> c(1, 1);
    std::vector<uint8_t> b(1, 1);
    int l = 0;
    int m = 1;
    uint8_t bb = 1;
    for (int n = 0; n < kNumRoots; ++n) {
        uint8_t d = s[n];
        for (int i = 1; i <= l; ++i) {
            if (i < static_cast<int>(c.size())) {
                d ^= gmul(c[i], s[n - i]);
            }
        }
        if (d == 0) {
            ++m;
            continue;
        }
        const std::vector<uint8_t> t = c;
        const uint8_t coef = gmul(d, ginv(bb));
        if (c.size() < b.size() + static_cast<std::size_t>(m)) {
            c.resize(b.size() + static_cast<std::size_t>(m), 0);
        }
        for (std::size_t i = 0; i < b.size(); ++i) {
            c[i + static_cast<std::size_t>(m)] ^= gmul(coef, b[i]);
        }
        if (2 * l <= n) {
            l = n + 1 - l;
            b = t;
            bb = d;
            m = 1;
        } else {
            ++m;
        }
    }
    if (l < 1 || l > kMaxCorrectable) {
        return -1;
    }

    // Chien search over exponents d = 0..254 (codeword index = 254 - d).
    std::vector<int> degrees;
    for (int dd = 0; dd < kBlock; ++dd) {
        uint8_t acc = 0;
        for (std::size_t m2 = 0; m2 <= static_cast<std::size_t>(l); ++m2) {
            if (m2 >= c.size()) {
                break;
            }
            const int e = ((-kRootGap * dd * static_cast<int>(m2)) %
                           kFieldOrder + kFieldOrder) %
                          kFieldOrder;
            acc ^= gmul(c[m2], gf().exp[e]);
        }
        if (acc == 0) {
            degrees.push_back(dd);
        }
    }
    if (static_cast<int>(degrees.size()) != l) {
        return -1;
    }

    // Magnitudes: S[j] = sum_k e_k * root_j^(degree_k).
    std::vector<std::vector<uint8_t>> a(
        static_cast<std::size_t>(l),
        std::vector<uint8_t>(static_cast<std::size_t>(l), 0));
    std::vector<uint8_t> rhs(static_cast<std::size_t>(l), 0);
    for (int j = 0; j < l; ++j) {
        rhs[static_cast<std::size_t>(j)] = s[j];
        for (int k = 0; k < l; ++k) {
            a[static_cast<std::size_t>(j)][static_cast<std::size_t>(k)] =
                gpow(roots()[static_cast<std::size_t>(j)],
                     degrees[static_cast<std::size_t>(k)]);
        }
    }
    std::vector<uint8_t> magnitudes;
    if (!solve_gf(a, rhs, magnitudes)) {
        return -1;
    }
    for (int k = 0; k < l; ++k) {
        const int index = 254 - degrees[static_cast<std::size_t>(k)];
        buf[index] ^= magnitudes[static_cast<std::size_t>(k)];
    }

    // Verify the corrected codeword satisfies all syndromes.
    for (int j = 0; j < kNumRoots; ++j) {
        uint8_t acc = 0;
        const uint8_t root = roots()[static_cast<std::size_t>(j)];
        for (int i = 0; i < kBlock; ++i) {
            acc ^= gmul(buf[i], gpow(root, 254 - i));
        }
        if (acc != 0) {
            return -1;
        }
    }

    if (dual_basis_) {
        const DualTables& t = dual_tables();
        for (int i = 0; i < kBlock; ++i) {
            buf[i] = t.to_dual[buf[i]];
        }
    }
    std::memcpy(block, buf, kBlock);
    return l;
}

void ReedSolomon::encode_interleaved(uint8_t* data, std::size_t len,
                                     int interleave) const {
    if (data == nullptr || interleave < 1 ||
        len < static_cast<std::size_t>(interleave) * kBlock) {
        return;
    }
    uint8_t buff[kBlock];
    for (int pos = 0; pos < interleave; ++pos) {
        for (int ii = 0; ii < kBlock; ++ii) {
            buff[ii] = data[static_cast<std::size_t>(ii * interleave + pos)];
        }
        encode_block(buff);
        for (int ii = 0; ii < kBlock; ++ii) {
            data[static_cast<std::size_t>(ii * interleave + pos)] = buff[ii];
        }
    }
}

void ReedSolomon::decode_interleaved(uint8_t* data, std::size_t len,
                                     int interleave,
                                     std::vector<int>& errors) const {
    errors.assign(static_cast<std::size_t>(std::max(0, interleave)), -1);
    if (data == nullptr || interleave < 1 ||
        len < static_cast<std::size_t>(interleave) * kBlock) {
        return;
    }
    uint8_t buff[kBlock];
    for (int pos = 0; pos < interleave; ++pos) {
        for (int ii = 0; ii < kBlock; ++ii) {
            buff[ii] = data[static_cast<std::size_t>(ii * interleave + pos)];
        }
        const int result = decode_block(buff);
        errors[static_cast<std::size_t>(pos)] = result;
        if (result >= 0) {
            for (int ii = 0; ii < kBlock; ++ii) {
                data[static_cast<std::size_t>(ii * interleave + pos)] = buff[ii];
            }
        }
    }
}

}  // namespace ed::fec
