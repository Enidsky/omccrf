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
        topologyReader.SetFileName("topologies/dsccp3-1.txt");
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
        Ptr<Node> C2 = Names::Find<Node>("C2");
        Ptr<Node> C3 = Names::Find<Node>("C3");

        Ptr<Node> P1 = Names::Find<Node>("P1");
        Ptr<Node> P2 = Names::Find<Node>("P2");
        Ptr<Node> P3 = Names::Find<Node>("P3");
        Ptr<Node> P4 = Names::Find<Node>("P4");


        ndn::AppHelper producerHelper("ns3::ndn::Producer");
        producerHelper.SetAttribute("PayloadSize", StringValue("1024"));

        // Register /dst1 prefix with global routing controller and
        // install producer that will satisfy Interests in /dst1 namespace
        // P1
        ndnGlobalRoutingHelper.AddOrigins("/A", P1);
        ndnGlobalRoutingHelper.AddOrigins("/B", P2);
        ndnGlobalRoutingHelper.AddOrigins("/C", P3);
        ndnGlobalRoutingHelper.AddOrigins("/A", P4);
        ndnGlobalRoutingHelper.AddOrigins("/B", P4);
        ndnGlobalRoutingHelper.AddOrigins("/C", P4);

        producerHelper.SetPrefix("/A");
        producerHelper.Install(P1);
        producerHelper.Install(P4);

        producerHelper.SetPrefix("/B");
        producerHelper.Install(P2);
        producerHelper.Install(P4);
        
        producerHelper.SetPrefix("/C");
        producerHelper.Install(P3);
        producerHelper.Install(P4);

        // Calculate all possible routes and install FIBs
        ndn::GlobalRoutingHelper::CalculateRoutes();

        ndn::FibHelper::AddRoute("R2", "/A", "R3", 0);
        ndn::FibHelper::AddRoute("R2", "/B", "R3", 0);
        ndn::FibHelper::AddRoute("R2", "/C", "R3", 0);

        ndn::FibHelper::AddRoute("R2", "/A", "R4", 0);
        ndn::FibHelper::AddRoute("R2", "/B", "R4", 0);
        ndn::FibHelper::AddRoute("R2", "/C", "R4", 0);

        ndn::FibHelper::AddRoute("R2", "/A", "R5", 0);
        ndn::FibHelper::AddRoute("R2", "/B", "R5", 0);
        ndn::FibHelper::AddRoute("R2", "/C", "R5", 0);

        ndn::FibHelper::AddRoute("R4", "/A", "R3", 0);
        ndn::FibHelper::AddRoute("R4", "/B", "R3", 0);
        ndn::FibHelper::AddRoute("R4", "/C", "R3", 0);

        ndn::FibHelper::AddRoute("R4", "/A", "R5", 0);
        ndn::FibHelper::AddRoute("R4", "/B", "R5", 0);
        ndn::FibHelper::AddRoute("R4", "/C", "R5", 0);

        ndn::AppHelper consumerHelper("ns3::ndn::ConsumerOMCCRF");
        consumerHelper.SetAttribute("CcAlgorithm", StringValue("AIMD"));
        consumerHelper.SetAttribute("Beta", DoubleValue(0.95));
        consumerHelper.SetAttribute("PMax", DoubleValue(0.05));
        // consumerHelper.SetAttribute("Dsz", UintegerValue(8696));
        // consumerHelper.SetAttribute("NumberOfContents", UintegerValue(100000));

        // C1
        consumerHelper.SetPrefix("/A");
        consumerHelper.Install("C1");

        // C2
        consumerHelper.SetPrefix("/B");
        consumerHelper.SetAttribute("DelayStart", UintegerValue(80000)); 
        consumerHelper.Install("C2");

        // C3
        consumerHelper.SetPrefix("/C");
        consumerHelper.SetAttribute("DelayStart", UintegerValue(160000));  
        consumerHelper.SetAttribute("DelayGreedy", IntegerValue(240000));
        consumerHelper.SetAttribute("GreedyRate", IntegerValue(10000000));
        consumerHelper.Install("C3");

        Simulator::Stop(Seconds(320.0));

        ndn::L3RateTracer::InstallAll("omccrf-throughput3-1.txt", Seconds(0.5));
        ndn::AppDelayTracer::InstallAll("omccrf-delay3-1.txt");
        L2RateTracer::InstallAll("omccrf-drop3-1.txt", Seconds(0.5));
        Simulator::Run();
        Simulator::Destroy();

        return 0;
    }

} // namespace ns3

int main(int argc, char *argv[])
{
    return ns3::main(argc, argv);
}
