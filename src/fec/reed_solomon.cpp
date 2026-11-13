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

// CCSDS dual-basis representation, from the reference implementation
// (SatDump/libcorrect). The generated trace-construction tables were close but
// not the CCSDS bit ordering, so real dual-basis codewords decoded as garbage:
// with these tables 13 of 120 frames on a known-good recording have all-zero
// syndromes, and none do without. The CCSDS RS chain transmits in dual basis,
// which is why SatDump defaults rs_dualbasis to true.
// Likewise the inverse permutation.
const uint8_t kFromDual[256] = {
    0x00, 0xcc, 0xac, 0x60, 0x79, 0xb5, 0xd5, 0x19, 0xf0, 0x3c, 0x5c, 0x90, 0x89, 0x45, 0x25, 0xe9,
    0xfd, 0x31, 0x51, 0x9d, 0x84, 0x48, 0x28, 0xe4, 0x0d, 0xc1, 0xa1, 0x6d, 0x74, 0xb8, 0xd8, 0x14,
    0x2e, 0xe2, 0x82, 0x4e, 0x57, 0x9b, 0xfb, 0x37, 0xde, 0x12, 0x72, 0xbe, 0xa7, 0x6b, 0x0b, 0xc7,
    0xd3, 0x1f, 0x7f, 0xb3, 0xaa, 0x66, 0x06, 0xca, 0x23, 0xef, 0x8f, 0x43, 0x5a, 0x96, 0xf6, 0x3a,
    0x42, 0x8e, 0xee, 0x22, 0x3b, 0xf7, 0x97, 0x5b, 0xb2, 0x7e, 0x1e, 0xd2, 0xcb, 0x07, 0x67, 0xab,
    0xbf, 0x73, 0x13, 0xdf, 0xc6, 0x0a, 0x6a, 0xa6, 0x4f, 0x83, 0xe3, 0x2f, 0x36, 0xfa, 0x9a, 0x56,
    0x6c, 0xa0, 0xc0, 0x0c, 0x15, 0xd9, 0xb9, 0x75, 0x9c, 0x50, 0x30, 0xfc, 0xe5, 0x29, 0x49, 0x85,
    0x91, 0x5d, 0x3d, 0xf1, 0xe8, 0x24, 0x44, 0x88, 0x61, 0xad, 0xcd, 0x01, 0x18, 0xd4, 0xb4, 0x78,
    0xc5, 0x09, 0x69, 0xa5, 0xbc, 0x70, 0x10, 0xdc, 0x35, 0xf9, 0x99, 0x55, 0x4c, 0x80, 0xe0, 0x2c,
    0x38, 0xf4, 0x94, 0x58, 0x41, 0x8d, 0xed, 0x21, 0xc8, 0x04, 0x64, 0xa8, 0xb1, 0x7d, 0x1d, 0xd1,
    0xeb, 0x27, 0x47, 0x8b, 0x92, 0x5e, 0x3e, 0xf2, 0x1b, 0xd7, 0xb7, 0x7b, 0x62, 0xae, 0xce, 0x02,
    0x16, 0xda, 0xba, 0x76, 0x6f, 0xa3, 0xc3, 0x0f, 0xe6, 0x2a, 0x4a, 0x86, 0x9f, 0x53, 0x33, 0xff,
    0x87, 0x4b, 0x2b, 0xe7, 0xfe, 0x32, 0x52, 0x9e, 0x77, 0xbb, 0xdb, 0x17, 0x0e, 0xc2, 0xa2, 0x6e,
    0x7a, 0xb6, 0xd6, 0x1a, 0x03, 0xcf, 0xaf, 0x63, 0x8a, 0x46, 0x26, 0xea, 0xf3, 0x3f, 0x5f, 0x93,
    0xa9, 0x65, 0x05, 0xc9, 0xd0, 0x1c, 0x7c, 0xb0, 0x59, 0x95, 0xf5, 0x39, 0x20, 0xec, 0x8c, 0x40,
    0x54, 0x98, 0xf8, 0x34, 0x2d, 0xe1, 0x81, 0x4d, 0xa4, 0x68, 0x08, 0xc4, 0xdd, 0x11, 0x71, 0xbd,
    };

const uint8_t kToDual[256] = {
    0x00, 0x7b, 0xaf, 0xd4, 0x99, 0xe2, 0x36, 0x4d, 0xfa, 0x81, 0x55, 0x2e, 0x63, 0x18, 0xcc, 0xb7,
    0x86, 0xfd, 0x29, 0x52, 0x1f, 0x64, 0xb0, 0xcb, 0x7c, 0x07, 0xd3, 0xa8, 0xe5, 0x9e, 0x4a, 0x31,
    0xec, 0x97, 0x43, 0x38, 0x75, 0x0e, 0xda, 0xa1, 0x16, 0x6d, 0xb9, 0xc2, 0x8f, 0xf4, 0x20, 0x5b,
    0x6a, 0x11, 0xc5, 0xbe, 0xf3, 0x88, 0x5c, 0x27, 0x90, 0xeb, 0x3f, 0x44, 0x09, 0x72, 0xa6, 0xdd,
    0xef, 0x94, 0x40, 0x3b, 0x76, 0x0d, 0xd9, 0xa2, 0x15, 0x6e, 0xba, 0xc1, 0x8c, 0xf7, 0x23, 0x58,
    0x69, 0x12, 0xc6, 0xbd, 0xf0, 0x8b, 0x5f, 0x24, 0x93, 0xe8, 0x3c, 0x47, 0x0a, 0x71, 0xa5, 0xde,
    0x03, 0x78, 0xac, 0xd7, 0x9a, 0xe1, 0x35, 0x4e, 0xf9, 0x82, 0x56, 0x2d, 0x60, 0x1b, 0xcf, 0xb4,
    0x85, 0xfe, 0x2a, 0x51, 0x1c, 0x67, 0xb3, 0xc8, 0x7f, 0x04, 0xd0, 0xab, 0xe6, 0x9d, 0x49, 0x32,
    0x8d, 0xf6, 0x22, 0x59, 0x14, 0x6f, 0xbb, 0xc0, 0x77, 0x0c, 0xd8, 0xa3, 0xee, 0x95, 0x41, 0x3a,
    0x0b, 0x70, 0xa4, 0xdf, 0x92, 0xe9, 0x3d, 0x46, 0xf1, 0x8a, 0x5e, 0x25, 0x68, 0x13, 0xc7, 0xbc,
    0x61, 0x1a, 0xce, 0xb5, 0xf8, 0x83, 0x57, 0x2c, 0x9b, 0xe0, 0x34, 0x4f, 0x02, 0x79, 0xad, 0xd6,
    0xe7, 0x9c, 0x48, 0x33, 0x7e, 0x05, 0xd1, 0xaa, 0x1d, 0x66, 0xb2, 0xc9, 0x84, 0xff, 0x2b, 0x50,
    0x62, 0x19, 0xcd, 0xb6, 0xfb, 0x80, 0x54, 0x2f, 0x98, 0xe3, 0x37, 0x4c, 0x01, 0x7a, 0xae, 0xd5,
    0xe4, 0x9f, 0x4b, 0x30, 0x7d, 0x06, 0xd2, 0xa9, 0x1e, 0x65, 0xb1, 0xca, 0x87, 0xfc, 0x28, 0x53,
    0x8e, 0xf5, 0x21, 0x5a, 0x17, 0x6c, 0xb8, 0xc3, 0x74, 0x0f, 0xdb, 0xa0, 0xed, 0x96, 0x42, 0x39,
    0x08, 0x73, 0xa7, 0xdc, 0x91, 0xea, 0x3e, 0x45, 0xf2, 0x89, 0x5d, 0x26, 0x6b, 0x10, 0xc4, 0xbf,
    };




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
    if (to_dual != nullptr) {
        std::memcpy(to_dual, kToDual, 256);
    }
    if (from_dual != nullptr) {
        std::memcpy(from_dual, kFromDual, 256);
    }
}

void ReedSolomon::configure(bool dual_basis) { dual_basis_ = dual_basis; }

void ReedSolomon::encode_block(uint8_t* block) const {
    if (block == nullptr) {
        return;
    }
    uint8_t buf[kBlock];
    std::memcpy(buf, block, kBlock);

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
        for (int i = 0; i < kBlock; ++i) {
            buf[i] = kToDual[buf[i]];
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
        for (int i = 0; i < kBlock; ++i) {
            buf[i] = kFromDual[buf[i]];
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

    // Deliberately left in the polynomial basis: the reference implementation only
    // converts back when decoding fails, so downstream sees normal-basis bytes.
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
