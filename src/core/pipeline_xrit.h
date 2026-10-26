// ElektroDump — xRIT (LRIT) pipeline (PLAN.md D3, milestone M4).
// Primary consumer of the IQ ring: BPSK demod, CCSDS FEC chain (Viterbi ->
// deframe -> derandomise -> RS), xRIT demux, then writes assembled LRIT files.
// IQ blocks are fanned out to the waterfall ring, mirroring GgakPipeline.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "core/pipeline.h"
#include "dsp/demod_psk.h"
#include "fec/ccsds_decoder.h"
#include "xrit/xrit_demux.h"

namespace ed {

class XritPipeline : public IPipeline {
public:
    XritPipeline();
    ~XritPipeline() override;

    XritPipeline(const XritPipeline&) = delete;
    XritPipeline& operator=(const XritPipeline&) = delete;

    PipelineMode mode() const override { return mode_; }
    bool configure(const Config& cfg, const PipelineParams& params) override;
    void attach_io(const PipelineIo& io) override;
    void start() override;
    void stop() override;
    PipelineStats stats() const override;

    double snr_db() const { return snr_db_.load(std::memory_order_relaxed); }
    const std::string& output_dir() const { return output_dir_; }

private:
    void dsp_loop();
    void publish_stats();
    void write_file(const xrit::XritFile& file);

    const Config* config_ = nullptr;
    PipelineParams params_;
    PipelineIo io_;
    PipelineMode mode_ = PipelineMode::LRIT;

    dsp::PskDemod demod_;
    fec::CcsdsDecoder decoder_;
    xrit::XritDemux demux_;

    std::string output_dir_ = "data/xrit";
    uint64_t file_counter_ = 0;

    std::thread thread_;
    std::atomic<bool> running_{false};

    std::atomic<double> snr_db_{0.0};
    std::atomic<double> ber_{0.0};
    std::atomic<bool> locked_{false};
    std::atomic<uint64_t> cadus_total_{0};
    std::atomic<uint64_t> cadus_uncorrectable_{0};
    std::atomic<uint64_t> files_total_{0};
    std::atomic<uint64_t> files_recovered_{0};
    std::chrono::steady_clock::time_point last_stats_{};
};

}  // namespace ed
