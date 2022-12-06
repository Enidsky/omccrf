#ifndef NDN_CONSUMER_OMCCRF_H
#define NDN_CONSUMER_OMCCRF_H

#include "ns3/ndnSIM/model/ndn-common.hpp"
#include "ndn-consumer-window.hpp"
#include <unordered_map>
#include <queue>

namespace ns3 {
    namespace ndn {

        enum CcAlgorithm {
            AIMD,
            BIC
        };
        class RouteMonitor {
        public:
            explicit RouteMonitor(size_t maxWindowSize = 30);

            bool appendRTT(double rtt);

            static double pMax;                            

        private:
            double RMin;                            
            double RMax;                            
            double pMin;                            
            double deltaPMax;                       
            double pr;                              
            size_t maxWindowSize;                   
            Time lastDecreaseTime;                  
            std::deque<double> sampleWindow;        
        };

        class ConsumerOMCCRF : public ConsumerWindow {
        public:
            static TypeId
            GetTypeId();

            ConsumerOMCCRF();

            virtual void
            OnData(shared_ptr<const Data> data) override;

            virtual void
            OnTimeout(uint32_t sequenceNum) override;

            virtual void
            OnNack(shared_ptr<const lp::Nack> nack);

            virtual void
            WillSendOutInterest(uint32_t sequenceNumber) override;

        private:
            void
            WindowIncrease();

            void
            WindowDecrease();

            void
            BicIncrease();

            void
            BicDecrease();

            void setPMax(double pMax) {
                RouteMonitor::pMax = pMax;
            }

            double getPMax() const {
                return RouteMonitor::pMax;
            }
            
        private:
            std::unordered_map<uint32_t, Time> inFlightInterest;
            std::unordered_map<uint64_t, std::shared_ptr<RouteMonitor>> routes;
            Time lastDecreaseTime;                   

            double m_beta;
            double m_ssthresh;
            double PMax;

            CcAlgorithm m_ccAlgorithm;
            // TCP BIC Parameters //
            //! Regular TCP behavior (including slow start) until this window size
            static constexpr uint32_t BIC_LOW_WINDOW = 14;

            //! Sets the maximum (linear) increase of TCP BIC. Should be between 8 and 64.
            static constexpr uint32_t BIC_MAX_INCREMENT = 16;

            // BIC variables:
            double m_bicMinWin; //!< last minimum cwnd
            double m_bicMaxWin; //!< last maximum cwnd
            double m_bicTargetWin;
            double m_bicSsCwnd;
            double m_bicSsTarget;
            bool m_isBicSs; //!< whether we are currently in the BIC slow start phase
        };
    }
}
#endif
