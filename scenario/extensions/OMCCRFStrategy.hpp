#ifndef NFD_DAEMON_FW_OMCCRF_STRATEGY_HPP
#define NFD_DAEMON_FW_OMCCRF_STRATEGY_HPP

#include <boost/random/mersenne_twister.hpp>
#include "face/face.hpp"
#include "fw/strategy.hpp"
#include "fw/retx-suppression-exponential.hpp"
#include "fw/algorithm.hpp"
#include "fw/process-nack-traits.hpp"
#include <unordered_map>
#include <string>
#include <limits>

namespace nfd
{
    namespace fw
    {
        class OMCCRFStrategy : public Strategy, public ProcessNackTraits<OMCCRFStrategy>
        {
        public:
            explicit OMCCRFStrategy(Forwarder &forwarder, const Name &name = getStrategyName());

            ~OMCCRFStrategy() override = default;

            void
            afterReceiveInterest(const FaceEndpoint &ingress, const Interest &interest,
                                 const shared_ptr<pit::Entry> &pitEntry) override;

            void
            beforeSatisfyInterest(const shared_ptr<pit::Entry> &pitEntry,
                                  const FaceEndpoint &ingress, const Data &data) override;

            void
            afterReceiveNack(const FaceEndpoint &ingress, const lp::Nack &nack,
                             const shared_ptr<pit::Entry> &pitEntry) override;

            void
            afterPITExpire(const shared_ptr<pit::Entry> &pitEntry);

            static const Name &
            getStrategyName();

        protected:
            void ensurePI(const std::string& prefix, face::FaceId faceId);

            void ensureAvgPIsAndWeights(const std::string& prefix, face::FaceId faceId);

            void increasePI(const std::string& prefix, face::FaceId faceId);

            void decreasePI(const std::string& prefix, face::FaceId faceId);

            void fibWeightUpdate(const face::FaceId &faceId, const std::string &prefix);

        protected:
            friend ProcessNackTraits<OMCCRFStrategy>;

        protected:
            std::unordered_map<std::string, std::shared_ptr<std::unordered_map<face::FaceId, uint64_t>>> PIs;

            std::unordered_map<std::string, std::shared_ptr<std::unordered_map<face::FaceId, double>>> avgPIs;

            std::unordered_map<std::string, std::shared_ptr<std::unordered_map<face::FaceId, double>>> weights;

            double alpha = 0.9;

        private:
            static const time::milliseconds RETX_SUPPRESSION_INITIAL;
            static const time::milliseconds RETX_SUPPRESSION_MAX;
            RetxSuppressionExponential m_retxSuppression;
        };
    }
}

#endif