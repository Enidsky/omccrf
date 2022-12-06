#include "ndn-consumer-omccrf.hpp"
#include <ndn-cxx/lp/tags.hpp>

NS_LOG_COMPONENT_DEFINE("ndn.ConsumerOMCCRF");

namespace ns3 {
    namespace ndn {
        NS_OBJECT_ENSURE_REGISTERED(ConsumerOMCCRF);


        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //// ConsumerOMCCRF
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        double RouteMonitor::pMax = 0.5;
        constexpr uint32_t ConsumerOMCCRF::BIC_MAX_INCREMENT;
        constexpr uint32_t ConsumerOMCCRF::BIC_LOW_WINDOW;

        TypeId
        ConsumerOMCCRF::GetTypeId()
        {
            static TypeId tid =
                TypeId("ns3::ndn::ConsumerOMCCRF")
                .SetGroupName("Ndn")
                .SetParent<ConsumerWindow>()
                .AddConstructor<ConsumerOMCCRF>()
                .AddAttribute("Beta",
                            "TCP Multiplicative Decrease factor",
                            DoubleValue(0.8),
                            MakeDoubleAccessor(&ConsumerOMCCRF::m_beta),
                            MakeDoubleChecker<double>())
                .AddAttribute("CcAlgorithm",
                                "Specify which window adaptation algorithm to use (AIMD, BIC)",
                                EnumValue(CcAlgorithm::AIMD),
                                MakeEnumAccessor(&ConsumerOMCCRF::m_ccAlgorithm),
                                MakeEnumChecker(CcAlgorithm::AIMD, "AIMD", CcAlgorithm::BIC, "BIC"))
                .AddAttribute("PMax",
                            "最大窗口下降概率",
                            DoubleValue(0.5),
                            MakeDoubleAccessor(&ConsumerOMCCRF::getPMax, &ConsumerOMCCRF::setPMax),
                            MakeDoubleChecker<double>());
            return tid;
        }

        ConsumerOMCCRF::ConsumerOMCCRF() 
            : m_ssthresh(std::numeric_limits<double>::max())
            , lastDecreaseTime(Simulator::Now()) {
        }

        void
        ConsumerOMCCRF::OnData(shared_ptr<const Data> data) {
            Consumer::OnData(data);
            // std::cout << "OnData" << std::endl;

            if (m_inFlight > static_cast<uint32_t>(0))
                m_inFlight--;

            uint64_t sequenceNum = data->getName().get(-1).toSequenceNumber();
            if (this->inFlightInterest.count(sequenceNum) != 0) {
                double rtt = (Simulator::Now() - this->inFlightInterest[sequenceNum]).ToDouble(Time::S);

                // 取出 RouteLabel，区分不同的路由
                auto routeLabel = data->getTag<lp::RouteLabelTag>();
                if (routeLabel != nullptr) {
                    auto routeLabelValue = routeLabel->get();
                    if (this->routes.count(routeLabelValue) == 0) {
                        this->routes[routeLabelValue] = std::make_shared<RouteMonitor>();
                    }
                    if (this->routes[routeLabelValue]->appendRTT(rtt)) {
                        this->WindowDecrease();
                    } else {
                        this->WindowIncrease();
                    }
                } else {
                    std::cout << "Data have no route label" << std::endl;
                }
            } else {
                std::cout << "inFlight Record is null???" << std::endl;
            }

            ScheduleNextPacket();
        }

        void
        ConsumerOMCCRF::OnTimeout(uint32_t sequenceNum) {
            this->WindowDecrease();

            
            if (m_inFlight > static_cast<uint32_t>(0))
                m_inFlight--;
            
            this->inFlightInterest.erase(sequenceNum);
            
            Consumer::OnTimeout(sequenceNum);
        }

        void
        ConsumerOMCCRF::OnNack(shared_ptr<const lp::Nack> nack) {
            // std::cout << "OnNack" << std::endl;
            Consumer::OnNack(nack);
        }

        void
        ConsumerOMCCRF::WillSendOutInterest(uint32_t sequenceNumber) {
            // std::cout << "Will Send Interest" << std::endl;
            ConsumerWindow::WillSendOutInterest(sequenceNumber);

            this->inFlightInterest[sequenceNumber] = Simulator::Now();
        }

        void
        ConsumerOMCCRF::WindowIncrease() {
            if (m_ccAlgorithm == CcAlgorithm::AIMD) {
                if (m_window < m_ssthresh) {
                    m_window += 1.0;
                }
                else {
                    m_window += (1.0 / m_window);
                }
            } else if (m_ccAlgorithm == CcAlgorithm::BIC) {
                BicIncrease();
            }
        }

        void
        ConsumerOMCCRF::WindowDecrease() {
            // std::cout << "WindowDecrease BIC" << std::endl;
            if (m_ccAlgorithm == CcAlgorithm::AIMD) {
                // Normal TCP Decrease:
                m_ssthresh = m_window * m_beta;
                if (m_ssthresh < 10) {
                    m_ssthresh = 10;
                }
                m_window = m_ssthresh;
            } else if (m_ccAlgorithm == CcAlgorithm::BIC) {
                BicDecrease();
            }
            lastDecreaseTime = Simulator::Now();
        }

        void
        ConsumerOMCCRF::BicIncrease()
        {
            // std::cout << "BIC WindowIncrease" << std::endl;

            if (m_window < BIC_LOW_WINDOW) {
                // Normal TCP AIMD behavior
                if (m_window < m_ssthresh) {
                    m_window = m_window + 1;
                }
                else {
                    m_window = m_window + 1.0 / m_window;
                }
            }
            else if (!m_isBicSs) {
                // Binary increase
                if (m_bicTargetWin - m_window < BIC_MAX_INCREMENT) { // Binary search
                    m_window += (m_bicTargetWin - m_window) / m_window;
                }
                else {
                    m_window += BIC_MAX_INCREMENT / m_window; // Additive increase
                }
                // FIX for equal double values.
                if (m_window + 0.00001 < m_bicMaxWin) {
                    m_bicMinWin = m_window;
                    m_bicTargetWin = (m_bicMaxWin + m_bicMinWin) / 2;
                }
                else {
                    m_isBicSs = true;
                    m_bicSsCwnd = 1;
                    m_bicSsTarget = m_window + 1.0;
                    m_bicMaxWin = std::numeric_limits<double>::max();
                }
            }
            else {
                // BIC slow start
                m_window += m_bicSsCwnd / m_window;
                if (m_window >= m_bicSsTarget) {
                    m_bicSsCwnd = 2 * m_bicSsCwnd;
                    m_bicSsTarget = m_window + m_bicSsCwnd;
                }
                if (m_bicSsCwnd >= BIC_MAX_INCREMENT) {
                    m_isBicSs = false;
                }
            }
        }

        void
        ConsumerOMCCRF::BicDecrease()
        {
            // BIC Decrease
            if (m_window >= BIC_LOW_WINDOW) {
                auto prev_max = m_bicMaxWin;
                m_bicMaxWin = m_window;
                m_window = m_window * m_beta;
                m_bicMinWin = m_window;
                if (prev_max > m_bicMaxWin) {
                    // Fast Convergence
                    m_bicMaxWin = (m_bicMaxWin + m_bicMinWin) / 2;
                }
                m_bicTargetWin = (m_bicMaxWin + m_bicMinWin) / 2;
            }
            else {
                // Normal TCP Decrease:
                m_ssthresh = m_window * m_beta;
                m_window = m_ssthresh;
            }
        }

        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //// RouteMonitor
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        RouteMonitor::RouteMonitor(size_t maxWindowSize)
            :RMin(std::numeric_limits<double>::max())
            , RMax(0)
            , pMin(10e-5)
            , deltaPMax(pMax - pMin)
            , pr(pMin)
            , maxWindowSize(maxWindowSize)
            , lastDecreaseTime(Simulator::Now()) {

        }

        bool
        RouteMonitor::appendRTT(double rtt) {
            if (sampleWindow.size() < maxWindowSize) {
                sampleWindow.push_back(rtt);
                return false;
            }

            sampleWindow.pop_front();
            sampleWindow.push_back(rtt);
            RMin = sampleWindow.back();
            RMax = sampleWindow.back();
            for (auto it = sampleWindow.begin(); it != sampleWindow.end(); it++) {
                if ((*it) < RMin) {
                    RMin = (*it);
                }
                if ((*it) > RMax) {
                    RMax = (*it);
                }
            }
            auto deltaRMax = RMax - RMin;
            if (deltaRMax < 10e-5) {
                pr = pMin;
            } else {
                pr = pMin + deltaPMax * (sampleWindow.back() - RMin) / deltaRMax;
            }

            double r = ((double)rand() / (RAND_MAX));
            
            return r <= pr;
        }
    }
}
