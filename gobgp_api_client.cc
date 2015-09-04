#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include <grpc/grpc.h>
#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/credentials.h>
#include "gobgp_api_client.grpc.pb.h"

extern "C" {
    // Gobgp library
    #include "libgobgp.h"
}

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using api::Grpc;

class GrpcClient {
    public:
        GrpcClient(std::shared_ptr<Channel> channel) : stub_(Grpc::NewStub(channel)) {}
        void GetAllActiveAnnounces() {
            ClientContext context;
            api::Arguments arguments;

            unsigned int AFI_IP = 1;
            unsigned int SAFI_UNICAST = 1;

            // 65537
            unsigned int route_family = AFI_IP<<16 | SAFI_UNICAST;
            //std::cout << "RF: "<< route_family << std::endl;

            arguments.set_rf(route_family);
            // We could specify certain neighbor here
            arguments.set_name("");
            arguments.set_resource(api::Resource::GLOBAL);

            auto destinations_list = stub_->GetRib(&context, arguments);

            api::Destination current_destination;

            std::cout << "List of announced prefixes" << std::endl << std::endl;
            while (destinations_list->Read(&current_destination)) {
                std::cout << "Prefix: " << current_destination.prefix() << std::endl;

                //std::cout << "Paths size: " << current_destination.paths_size() << std::endl;

                api::Path my_path = current_destination.paths(0);

                buf my_nlri;
                my_nlri.value = (char*)my_path.nlri().c_str();
                my_nlri.len = my_path.nlri().size();

                path_t gobgp_lib_path;
                gobgp_lib_path.nlri = my_nlri;

                // std::cout << "NLRI:" << my_path.nlri() << std::endl;
                std::cout << decode_path(&gobgp_lib_path) << std::endl; 
            }

            Status status = destinations_list->Finish();
            if (!status.ok()) {
                // error_message
                std::cout << "Problem with RPC: " << status.error_code() << std::endl;
            } else {
                // std::cout << "RPC working well" << std::endl;
            }
        }

        void AnnounceUnicastPrefix() {
            std::string next_hop = "10.10.1.99";

            api::ModPathArguments current_mod_path_arguments;
            current_mod_path_arguments.set_resource(api::GLOBAL);

            api::Path current_path; 
            // current_path->set_is_withdraw();

            current_path.set_nlri("10.10.20.33/22");
        }

        std::string GetAllNeighbor(std::string neighbor_ip) {
        api::Arguments request;
        request.set_rf(4);
        request.set_name(neighbor_ip);

        ClientContext context;

        api::Peer peer;
        grpc::Status status = stub_->GetNeighbor(&context, request, &peer);

        if (status.ok()) {
            api::PeerConf peer_conf = peer.conf();
            api::PeerInfo peer_info = peer.info();

            std::stringstream buffer;
  
            buffer
                << "Peer AS: " << peer_conf.remote_as() << "\n"
                << "Peer router id: " << peer_conf.id() << "\n"
                << "Peer flops: " << peer_info.flops() << "\n"
                << "BGP state: " << peer_info.bgp_state();

            return buffer.str();
        } else {
            return "Something wrong"; 
        }

    }

    private:
        std::unique_ptr<Grpc::Stub> stub_;
};

int main(int argc, char** argv) {
    GrpcClient gobgp_client(grpc::CreateChannel("localhost:8080", grpc::InsecureCredentials()));
 
    //std::string reply = gobgp_client.GetAllNeighbor("213.133.111.200");
    //std::cout << "We received: " << reply << std::endl;

    gobgp_client.AnnounceUnicastPrefix();
    gobgp_client.GetAllActiveAnnounces();

    return 0;
}
