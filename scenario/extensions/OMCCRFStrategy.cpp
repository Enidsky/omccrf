#include "OMCCRFStrategy.hpp"
#include <ndn-cxx/lp/tags.hpp>

NFD_LOG_INIT(OMCCRFStrategy);

namespace nfd
{
    namespace fw
    {
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //// OMCCRFStrategy
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        const time::milliseconds OMCCRFStrategy::RETX_SUPPRESSION_INITIAL(10);
        const time::milliseconds OMCCRFStrategy::RETX_SUPPRESSION_MAX(250);

        OMCCRFStrategy::OMCCRFStrategy(nfd::Forwarder &forwarder, const ndn::Name &name)
            : Strategy(forwarder), ProcessNackTraits<OMCCRFStrategy>(this), m_retxSuppression(RETX_SUPPRESSION_INITIAL,
                                                                                              RetxSuppressionExponential::DEFAULT_MULTIPLIER,
                                                                                              RETX_SUPPRESSION_MAX)
        {
            this->setInstanceName(makeInstanceName(name, getStrategyName()));
        }

        const Name &
        OMCCRFStrategy::getStrategyName()
        {
            static Name strategyName("/localhost/nfd/strategy/PCON/%FD%01");
            return strategyName;
        }

        void
        OMCCRFStrategy::afterReceiveInterest(const nfd::FaceEndpoint &ingress, const ndn::Interest &interest,
                                             const std::shared_ptr<nfd::pit::Entry> &pitEntry)
        {
            // std::cout << "afterReceiveInterest" << std::endl;
            RetxSuppressionResult suppression = m_retxSuppression.decidePerPitEntry(*pitEntry);
            if (suppression == RetxSuppressionResult::SUPPRESS)
            {
                NFD_LOG_DEBUG(interest << " from=" << ingress << " suppressed");
                return;
            }

            const fib::Entry &fibEntry = this->lookupFib(*pitEntry);
            const fib::NextHopList &nexthops = fibEntry.getNextHops();

            Face *outFace = nullptr;

            std::string prefix = interest.getName().getPrefix(1).toUri();

            if (ingress.face.getScope() != ndn::nfd::FACE_SCOPE_NON_LOCAL)
            {
                auto selected = std::find_if(nexthops.begin(), nexthops.end(), [&](const auto &nexthop)
                                             { return isNextHopEligible(ingress.face, interest, nexthop, pitEntry); });
                if (selected != nexthops.end())
                {
                    outFace = &((*selected).getFace());
                }
            }
            else
            {
                double r = ((double)rand() / (RAND_MAX));

                double totalWeight = 0;

                // Add all eligbile faces to list (excludes current downstream)
                std::vector<Face *> eligbleFaces;
                for (auto &n : fibEntry.getNextHops())
                {
                    if (isNextHopEligible(ingress.face, interest, n, pitEntry))
                    {
                        // Add up percentage Sum.
                        auto faceId = n.getFace().getId();
                        this->ensureAvgPIsAndWeights(prefix, faceId);
                        totalWeight += (*this->weights[prefix])[faceId];
                        eligbleFaces.push_back(&n.getFace());
                    }
                }

                if (eligbleFaces.size() < 1)
                {
                    return;
                }
                else if (eligbleFaces.size() == 1)
                {
                    outFace = eligbleFaces.front();
                }
                else
                {
                    double forwPerc = 0;
                    for (auto face : eligbleFaces)
                    {
                        forwPerc += ((*this->weights[prefix])[face->getId()] / totalWeight);
                        if (r < forwPerc)
                        {
                            outFace = face;
                            break;
                        }
                    }
                }
            }

            if (outFace == nullptr)
            {
                lp::NackHeader nackHeader;
                nackHeader.setReason(lp::NackReason::NO_ROUTE);
                this->sendNack(pitEntry, ingress, nackHeader);
                this->rejectPendingInterest(pitEntry);
                return;
            }
            else
            {
                this->increasePI(prefix, outFace->getId());
                this->fibWeightUpdate(outFace->getId(), prefix);

                this->sendInterest(pitEntry, FaceEndpoint(*outFace, 0), interest);
            }
        }

        void
        OMCCRFStrategy::beforeSatisfyInterest(const shared_ptr<pit::Entry> &pitEntry,
                                              const FaceEndpoint &ingress, const Data &data)
        {
            std::string prefix = data.getName().getPrefix(1).toUri();
            this->decreasePI(prefix, ingress.face.getId());

            this->fibWeightUpdate(ingress.face.getId(), prefix);

            if (ingress.face.getId() > 256)
            {
                auto routeLabel = data.getTag<lp::RouteLabelTag>();
                uint64_t newRouteLabelValue = 0;
                if (routeLabel == nullptr)
                {
                    newRouteLabelValue = ingress.face.getId() - 256;
                }
                else
                {
                    newRouteLabelValue = routeLabel->get() * 10 + (ingress.face.getId() - 256);
                }
                data.setTag(make_shared<lp::RouteLabelTag>(newRouteLabelValue));
            }
        }

        void 
        OMCCRFStrategy::afterReceiveNack(const nfd::FaceEndpoint &ingress, const ndn::lp::Nack &nack,
                                            const std::shared_ptr<nfd::pit::Entry> &pitEntry)
        {
            this->processNack(ingress.face, nack, pitEntry);
        }

        void
        OMCCRFStrategy::afterPITExpire(const shared_ptr<pit::Entry> &pitEntry)
        {
            std::string prefix = pitEntry->getName().getPrefix(1).toUri();

            for (auto &outRecord : pitEntry->getOutRecords())
            {
                this->decreasePI(prefix, outRecord.getFace().getId());

                this->fibWeightUpdate(outRecord.getFace().getId(), prefix);
            }
        }

        void
        OMCCRFStrategy::ensurePI(const std::string &prefix, face::FaceId faceId)
        {
            if (this->PIs.count(prefix) == 0)
            {
                this->PIs[prefix] = std::make_shared<std::unordered_map<face::FaceId, uint64_t>>();
            }
            if (this->PIs[prefix]->count(faceId) == 0)
            {
                (*this->PIs[prefix])[faceId] = 0;
            }
        }

        void
        OMCCRFStrategy::increasePI(const std::string &prefix, face::FaceId faceId)
        {
            this->ensurePI(prefix, faceId);
            (*this->PIs[prefix])[faceId]++;
        }

        void
        OMCCRFStrategy::decreasePI(const std::string &prefix, face::FaceId faceId)
        {
            this->ensurePI(prefix, faceId);
            (*this->PIs[prefix])[faceId]--;
        }

        void
        OMCCRFStrategy::ensureAvgPIsAndWeights(const std::string &prefix, face::FaceId faceId)
        {
            // ensure avgPIs
            if (this->avgPIs.count(prefix) == 0)
            {
                this->avgPIs[prefix] = std::make_shared<std::unordered_map<face::FaceId, double>>();
            }
            if (this->avgPIs[prefix]->count(faceId) == 0)
            {
                (*this->avgPIs[prefix])[faceId] = 0.0;
            }

            // ensure weights
            if (this->weights.count(prefix) == 0)
            {
                this->weights[prefix] = std::make_shared<std::unordered_map<face::FaceId, double>>();
            }
            if (this->weights[prefix]->count(faceId) == 0)
            {
                (*this->weights[prefix])[faceId] = 1.0;
            }
        }

        void
        OMCCRFStrategy::fibWeightUpdate(const face::FaceId &faceId, const std::string &prefix)
        {
            this->ensureAvgPIsAndWeights(prefix, faceId);

            (*this->avgPIs[prefix])[faceId] = alpha * (*this->avgPIs[prefix])[faceId] + (1 - alpha) *
                                                                                            (*this->PIs[prefix])[faceId];

            double avgPI = (*this->avgPIs[prefix])[faceId];
            if (avgPI < 1)
            {
                avgPI = 1;
            }
            (*this->weights[prefix])[faceId] = 1.0 / avgPI;
        }
    }
}