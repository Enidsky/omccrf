#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/ndnSIM-module.h"
#include "../extensions/OMCCRFStrategy.hpp"

namespace ns3
{

    int
    main(int argc, char *argv[])
    {
        CommandLine cmd;
        cmd.Parse(argc, argv);

        AnnotatedTopologyReader topologyReader("", 25);
        topologyReader.SetFileName("topologies/test-1.txt");
        topologyReader.Read();

        // Install NDN stack on all nodes
        ndn::StackHelper ndnHelper;
        ndnHelper.setPolicy("nfd::cs::lru");
        // ndnHelper.setCsSize(10000);
        ndnHelper.setCsSize(0);
        ndnHelper.InstallAll();

        // Choosing forwarding strategy
        // ndn::StrategyChoiceHelper::InstallAll("/", "/localhost/nfd/strategy/best-route");
        ndn::StrategyChoiceHelper::InstallAll<nfd::fw::OMCCRFStrategy>("/");

        // Installing global routing interface on all nodes
        ndn::GlobalRoutingHelper ndnGlobalRoutingHelper;
        ndnGlobalRoutingHelper.InstallAll();

        // Getting containers for the consumer/producer
        Ptr<Node> C1 = Names::Find<Node>("C1");

        Ptr<Node> P1 = Names::Find<Node>("P1");

        ndn::AppHelper producerHelper("ns3::ndn::Producer");
        producerHelper.SetAttribute("PayloadSize", StringValue("1024"));

        // Register /dst1 prefix with global routing controller and
        // install producer that will satisfy Interests in /dst1 namespace
        // P1
        ndnGlobalRoutingHelper.AddOrigins("/A", P1);

        producerHelper.SetPrefix("/A");
        producerHelper.Install(P1);

        // Calculate all possible routes and install FIBs
        ndn::GlobalRoutingHelper::CalculateRoutes();

        ndn::AppHelper consumerHelper("ns3::ndn::ConsumerOMCCRF");
        consumerHelper.SetAttribute("CcAlgorithm", StringValue("AIMD"));
        consumerHelper.SetAttribute("Beta", DoubleValue(0.98));
        consumerHelper.SetAttribute("PMax", DoubleValue(0.05));
        // consumerHelper.SetAttribute("Dsz", UintegerValue(8696));
        // consumerHelper.SetAttribute("NumberOfContents", UintegerValue(100000));

        // C1
        consumerHelper.SetPrefix("/A");
        consumerHelper.Install("C1");

        Simulator::Stop(Seconds(320.0));

        ndn::L3RateTracer::InstallAll("test-throughput-320.txt", Seconds(0.5));
        ndn::AppDelayTracer::InstallAll("test-delay-320.txt");
        L2RateTracer::InstallAll("test-drop-320.txt", Seconds(0.5));
        Simulator::Run();
        Simulator::Destroy();

        return 0;
    }

} // namespace ns3

int main(int argc, char *argv[])
{
    return ns3::main(argc, argv);
}
