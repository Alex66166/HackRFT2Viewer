/*
 * HackRF T2 Viewer - lightweight cross-thread DVB-T2 FEC profiler
 * GPL-3.0-or-later
 */
#ifndef FEC_PIPELINE_PROFILE_H
#define FEC_PIPELINE_PROFILE_H

#include <QElapsedTimer>
#include <QtGlobal>

#include <atomic>
#include <algorithm>

struct fec_pipeline_profile_state
{
    std::atomic<quint64> tiCalls{0}, tiCells{0}, tiNs{0}, tiQueuedBlocks{0}, tiSkippedBlocks{0};
    std::atomic<quint64> qamCalls{0}, qamCells{0}, qamFecBlocks{0}, qamNs{0}, qamWaitNs{0};
    std::atomic<quint64> ldpcCalls{0}, ldpcBlocks{0}, ldpcNs{0}, ldpcWaitNs{0};
    std::atomic<quint64> ldpcIterations{0}, ldpcNoConverge{0};
    std::atomic<quint64> bchCalls{0}, bchFrames{0}, bchFailed{0}, bchNs{0}, bchWaitNs{0};
    std::atomic<quint64> bbCalls{0}, bbNs{0}, bbWaitNs{0};

    std::atomic<int> qamPending{0}, qamPendingMax{0};
    std::atomic<int> ldpcPending{0}, ldpcPendingMax{0};
    std::atomic<int> bchPending{0}, bchPendingMax{0};
    std::atomic<int> bbPending{0}, bbPendingMax{0};

    std::atomic<int> selectedPlp{-1};
    std::atomic<int> numPlp{0};
};

inline fec_pipeline_profile_state &fec_pipeline_profile()
{
    static fec_pipeline_profile_state state;
    return state;
}

inline void fec_update_max(std::atomic<int> &maximum, int value)
{
    int old = maximum.load(std::memory_order_relaxed);
    while(value > old &&
          !maximum.compare_exchange_weak(old, value, std::memory_order_relaxed)) {}
}

class fec_scope_timer
{
public:
    explicit fec_scope_timer(std::atomic<quint64> &counter)
        : counter_(&counter)
    {
        timer_.start();
    }
    ~fec_scope_timer()
    {
        if(counter_)
            counter_->fetch_add(static_cast<quint64>(timer_.nsecsElapsed()),
                                std::memory_order_relaxed);
    }
private:
    std::atomic<quint64> *counter_;
    QElapsedTimer timer_;
};

class fec_pending_token
{
public:
    fec_pending_token(std::atomic<int> &current, std::atomic<int> &maximum)
        : current_(&current)
    {
        const int value = current_->fetch_add(1, std::memory_order_relaxed) + 1;
        fec_update_max(maximum, value);
    }
    ~fec_pending_token()
    {
        if(current_)
            current_->fetch_sub(1, std::memory_order_relaxed);
    }
    fec_pending_token(const fec_pending_token &) = delete;
    fec_pending_token &operator=(const fec_pending_token &) = delete;
private:
    std::atomic<int> *current_;
};

struct fec_pipeline_snapshot
{
    quint64 tiCalls=0,tiCells=0,tiNs=0,tiQueuedBlocks=0,tiSkippedBlocks=0;
    quint64 qamCalls=0,qamCells=0,qamFecBlocks=0,qamNs=0,qamWaitNs=0;
    quint64 ldpcCalls=0,ldpcBlocks=0,ldpcNs=0,ldpcWaitNs=0,ldpcIterations=0,ldpcNoConverge=0;
    quint64 bchCalls=0,bchFrames=0,bchFailed=0,bchNs=0,bchWaitNs=0;
    quint64 bbCalls=0,bbNs=0,bbWaitNs=0;
    int qamPending=0,qamPendingMax=0,ldpcPending=0,ldpcPendingMax=0;
    int bchPending=0,bchPendingMax=0,bbPending=0,bbPendingMax=0;
    int selectedPlp=-1,numPlp=0;
};

inline fec_pipeline_snapshot take_fec_pipeline_snapshot()
{
    auto &s=fec_pipeline_profile();
    fec_pipeline_snapshot r;
#define FEC_TAKE(name) r.name=s.name.exchange(0,std::memory_order_relaxed)
    FEC_TAKE(tiCalls); FEC_TAKE(tiCells); FEC_TAKE(tiNs);
    FEC_TAKE(tiQueuedBlocks); FEC_TAKE(tiSkippedBlocks);
    FEC_TAKE(qamCalls); FEC_TAKE(qamCells); FEC_TAKE(qamFecBlocks);
    FEC_TAKE(qamNs); FEC_TAKE(qamWaitNs);
    FEC_TAKE(ldpcCalls); FEC_TAKE(ldpcBlocks); FEC_TAKE(ldpcNs); FEC_TAKE(ldpcWaitNs);
    FEC_TAKE(ldpcIterations); FEC_TAKE(ldpcNoConverge);
    FEC_TAKE(bchCalls); FEC_TAKE(bchFrames); FEC_TAKE(bchFailed);
    FEC_TAKE(bchNs); FEC_TAKE(bchWaitNs);
    FEC_TAKE(bbCalls); FEC_TAKE(bbNs); FEC_TAKE(bbWaitNs);
#undef FEC_TAKE
    r.qamPending=s.qamPending.load(std::memory_order_relaxed);
    r.ldpcPending=s.ldpcPending.load(std::memory_order_relaxed);
    r.bchPending=s.bchPending.load(std::memory_order_relaxed);
    r.bbPending=s.bbPending.load(std::memory_order_relaxed);
    r.qamPendingMax=s.qamPendingMax.exchange(r.qamPending,std::memory_order_relaxed);
    r.ldpcPendingMax=s.ldpcPendingMax.exchange(r.ldpcPending,std::memory_order_relaxed);
    r.bchPendingMax=s.bchPendingMax.exchange(r.bchPending,std::memory_order_relaxed);
    r.bbPendingMax=s.bbPendingMax.exchange(r.bbPending,std::memory_order_relaxed);
    r.selectedPlp=s.selectedPlp.load(std::memory_order_relaxed);
    r.numPlp=s.numPlp.load(std::memory_order_relaxed);
    return r;
}

inline void reset_fec_pipeline_profile()
{
    (void)take_fec_pipeline_snapshot();
    auto &s=fec_pipeline_profile();
    s.selectedPlp.store(-1,std::memory_order_relaxed);
    s.numPlp.store(0,std::memory_order_relaxed);
}

#endif // FEC_PIPELINE_PROFILE_H
