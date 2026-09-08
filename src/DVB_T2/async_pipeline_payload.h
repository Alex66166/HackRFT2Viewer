/*
 * HackRF T2 Viewer - safe payload ownership for asynchronous DVB-T2 stages
 * GPL-3.0-or-later
 */
#ifndef ASYNC_PIPELINE_PAYLOAD_H
#define ASYNC_PIPELINE_PAYLOAD_H

#include "dvbt2_definition.h"

#include <QSemaphore>

#include <algorithm>
#include <memory>
#include <vector>

// Qt defines `slots` as a preprocessor macro.  This helper is included only
// from .cpp files after QObject-derived class declarations, so it is safe to
// remove that macro here and use normal C++ identifiers without collisions.
#ifdef slots
#undef slots
#endif

// l1_postsignalling contains several raw pointers.  A shallow copy is safe
// only while the producer is blocked.  The asynchronous DSP pipeline needs
// independent storage for the fields consumed by the downstream stages.
class owned_l1_post
{
public:
    explicit owned_l1_post(const l1_postsignalling &source)
        : value(source)
    {
        const int plpCount = std::max(0, source.num_plp);
        const int auxCount = std::max(0, source.num_aux);

        if (plpCount > 0 && source.plp != nullptr)
            plp.assign(source.plp, source.plp + plpCount);
        if (auxCount > 0 && source.aux != nullptr)
            aux.assign(source.aux, source.aux + auxCount);
        if (plpCount > 0 && source.dyn.plp != nullptr)
            dynPlp.assign(source.dyn.plp, source.dyn.plp + plpCount);
        if (plpCount > 0 && source.dyn_next.plp != nullptr)
            dynNextPlp.assign(source.dyn_next.plp, source.dyn_next.plp + plpCount);
        if (auxCount > 0 && source.dyn.aux_private_dyn != nullptr)
            dynAux.assign(source.dyn.aux_private_dyn,
                          source.dyn.aux_private_dyn + auxCount);
        if (auxCount > 0 && source.dyn_next.aux_private_dyn != nullptr)
            dynNextAux.assign(source.dyn_next.aux_private_dyn,
                              source.dyn_next.aux_private_dyn + auxCount);

        value.plp = plp.empty() ? nullptr : plp.data();
        value.aux = aux.empty() ? nullptr : aux.data();
        value.dyn.plp = dynPlp.empty() ? nullptr : dynPlp.data();
        value.dyn_next.plp = dynNextPlp.empty() ? nullptr : dynNextPlp.data();
        value.dyn.aux_private_dyn = dynAux.empty() ? nullptr : dynAux.data();
        value.dyn_next.aux_private_dyn = dynNextAux.empty() ? nullptr : dynNextAux.data();

        // RF signalling is not consumed by the deinterleaver/FEC/BBFRAME
        // chain. Keep the original pointer for compatibility; its owner is
        // p2_symbol and it remains allocated for the lifetime of that stage.
        value.rf = source.rf;
    }

    owned_l1_post(const owned_l1_post &) = delete;
    owned_l1_post &operator=(const owned_l1_post &) = delete;

    l1_postsignalling value{};

private:
    std::vector<l1_postsignalling_plp> plp;
    std::vector<l1_postsignalling_aux> aux;
    std::vector<dynamic_plp> dynPlp;
    std::vector<dynamic_plp> dynNextPlp;
    std::vector<int> dynAux;
    std::vector<int> dynNextAux;
};

// RAII slot for a bounded queued stage. If a queued metacall is discarded
// during shutdown, destruction of the captured permit still releases the
// semaphore, so a later restart cannot inherit a permanently reduced queue.
class async_queue_permit
{
public:
    explicit async_queue_permit(QSemaphore &semaphore)
        : semaphore_(&semaphore)
    {
        semaphore_->acquire(1);
    }

    ~async_queue_permit()
    {
        if (semaphore_ != nullptr)
            semaphore_->release(1);
    }

    async_queue_permit(const async_queue_permit &) = delete;
    async_queue_permit &operator=(const async_queue_permit &) = delete;

private:
    QSemaphore *semaphore_ = nullptr;
};

inline std::shared_ptr<async_queue_permit> acquire_async_queue_slot(QSemaphore &semaphore)
{
    return std::make_shared<async_queue_permit>(semaphore);
}

#endif // ASYNC_PIPELINE_PAYLOAD_H
