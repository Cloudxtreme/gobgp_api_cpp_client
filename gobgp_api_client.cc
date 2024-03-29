#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/credentials.h>
#include "gobgp_api_client.grpc.pb.h"

#include <dlfcn.h>

extern "C" {
    // Gobgp library
    #include "libgobgp.h"
}

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using gobgpapi::GobgpApi;

// Create function pointers
typedef path* (*serialize_path_dynamic_t)(int p0, char* p1);
typedef char* (*decode_path_dynamic_t)(path* p0);

serialize_path_dynamic_t serialize_path_dynamic = NULL;
decode_path_dynamic_t decode_path_dynamic = NULL;

class GrpcClient {
    public:
        GrpcClient(std::shared_ptr<Channel> channel) : stub_(GobgpApi::NewStub(channel)) {}
        void GetAllActiveAnnounces(unsigned int route_family) {
            ClientContext context;
            gobgpapi::Arguments arguments;

            arguments.set_rf(route_family);
            // We could specify certain neighbor here
            arguments.set_name("");
            arguments.set_resource(gobgpapi::Resource::GLOBAL);

            auto destinations_list = stub_->GetRib(&context, arguments);

            gobgpapi::Destination current_destination;

            std::cout << "List of announced prefixes for route family: " << route_family << std::endl << std::endl;
            while (destinations_list->Read(&current_destination)) {
                std::cout << "Prefix: " << current_destination.prefix() << std::endl;
    
                //std::cout << "Paths size: " << current_destination.paths_size() << std::endl;

                gobgpapi::Path my_path = current_destination.paths(0);

                // std::cout << "Pattrs size: " << my_path.pattrs_size() << std::endl;

                buf my_nlri;
                my_nlri.value = (char*)my_path.nlri().c_str();
                my_nlri.len = my_path.nlri().size();

                path_t gobgp_lib_path;
                gobgp_lib_path.nlri = my_nlri;
                // Not used in library code!
                gobgp_lib_path.path_attributes_cap = 0;
                gobgp_lib_path.path_attributes_len = my_path.pattrs_size();

                buf* my_path_attributes[ my_path.pattrs_size() ];
                for (int i = 0; i < my_path.pattrs_size(); i++) {
                    my_path_attributes[i] = (buf*)malloc(sizeof(buf));
                    my_path_attributes[i]->len = my_path.pattrs(i).size();
                    my_path_attributes[i]->value = (char*)my_path.pattrs(i).c_str();
                }
            
                gobgp_lib_path.path_attributes = my_path_attributes;

                std::cout << "NLRI: " << decode_path_dynamic(&gobgp_lib_path) << std::endl; 
            }

            Status status = destinations_list->Finish();
            if (!status.ok()) {
                // error_message
                std::cout << "Problem with RPC: " << status.error_code() << " message " << status.error_message() << std::endl;
            } else {
                // std::cout << "RPC working well" << std::endl;
            }
        }

        void AnnounceFlowSpecPrefix() {
            const gobgpapi::ModPathArguments current_mod_path_arguments;

            unsigned int AFI_IP = 1;
            unsigned int SAFI_FLOW_SPEC_UNICAST = 133;
            unsigned int ipv4_flow_spec_route_family = AFI_IP<<16 | SAFI_FLOW_SPEC_UNICAST;   

            gobgpapi::Path* current_path = new gobgpapi::Path;
            // If you want withdraw, please use it 
            // current_path->set_is_withdraw(true);

            /*
            buf:
                char *value;
                int len;

            path:
                buf   nlri;
                buf** path_attributes;
                int   path_attributes_len;
                int   path_attributes_cap;
            */

            path* path_c_struct = serialize_path_dynamic(ipv4_flow_spec_route_family, (char*)"match destination 10.0.0.0/24 protocol tcp source 20.0.0.0/24 then redirect 10:10");

            // printf("Decoded NLRI output: %s, length %d raw string length: %d\n", decode_path(path_c_struct), path_c_struct->nlri.len, strlen(path_c_struct->nlri.value));

            for (int path_attribute_number = 0; path_attribute_number < path_c_struct->path_attributes_len; path_attribute_number++) {
                current_path->add_pattrs(path_c_struct->path_attributes[path_attribute_number]->value, 
                    path_c_struct->path_attributes[path_attribute_number]->len);
            }

            current_path->set_nlri(path_c_struct->nlri.value, path_c_struct->nlri.len);

            gobgpapi::ModPathArguments request;
            request.set_resource(gobgpapi::Resource::GLOBAL);

            google::protobuf::RepeatedPtrField< ::gobgpapi::Path >* current_path_list = request.mutable_paths(); 
            current_path_list->AddAllocated(current_path);
            request.set_name("");

            ClientContext context;

            gobgpapi::Error return_error;

            // result is a std::unique_ptr<grpc::ClientWriter<gobgpapi::ModPathArguments> >
            auto send_stream = stub_->ModPath(&context, &return_error);

            bool write_result = send_stream->Write(request);

            if (!write_result) {
                std::cout << "Write to API failed\n";
            }

            // Finish all writes
            send_stream->WritesDone();

            auto status = send_stream->Finish();
    
            if (status.ok()) {
                //std::cout << "modpath executed correctly" << std::cout; 
            } else {
                std::cout << "modpath failed with code: " << status.error_code()
                    << " message " << status.error_message() << std::endl;
            }
        }

        void AnnounceUnicastPrefix() {
            const gobgpapi::ModPathArguments current_mod_path_arguments;

            unsigned int AFI_IP = 1;
            unsigned int SAFI_UNICAST = 1;
            unsigned int ipv4_unicast_route_family = AFI_IP<<16 | SAFI_UNICAST;

            gobgpapi::Path* current_path = new gobgpapi::Path;
            // If you want withdraw, please use it 
            // current_path->set_is_withdraw(true);

            /*
            buf:
                char *value;
                int len;

            path:
                buf   nlri;
                buf** path_attributes;
                int   path_attributes_len;
                int   path_attributes_cap;
            */
            std::string announced_prefix = "10.10.20.33/32";
            std::string announced_prefix_nexthop = "10.10.1.99";

            std::string announce_line = announced_prefix + " nexthop " + announced_prefix_nexthop;

            path* path_c_struct = serialize_path_dynamic(ipv4_unicast_route_family, (char*)announce_line.c_str());

            if (path_c_struct == NULL) {
                std::cerr << "Could not generate path\n";
                exit(-1);
            }

            // printf("Decoded NLRI output: %s, length %d raw string length: %d\n", decode_path(path_c_struct), path_c_struct->nlri.len, strlen(path_c_struct->nlri.value));

            for (int path_attribute_number = 0; path_attribute_number < path_c_struct->path_attributes_len; path_attribute_number++) {
                current_path->add_pattrs(path_c_struct->path_attributes[path_attribute_number]->value, 
                    path_c_struct->path_attributes[path_attribute_number]->len);
            }

            current_path->set_nlri(path_c_struct->nlri.value, path_c_struct->nlri.len);

            gobgpapi::ModPathArguments request;
            request.set_resource(gobgpapi::Resource::GLOBAL);
            google::protobuf::RepeatedPtrField< ::gobgpapi::Path >* current_path_list = request.mutable_paths(); 
            current_path_list->AddAllocated(current_path);
            request.set_name("");

            ClientContext context;

            gobgpapi::Error return_error;

            // result is a std::unique_ptr<grpc::ClientWriter<api::ModPathArguments> >
            auto send_stream = stub_->ModPath(&context, &return_error);

            bool write_result = send_stream->Write(request);

            if (!write_result) {
                std::cout << "Write to API failed\n";
            }

            // Finish all writes
            send_stream->WritesDone();

            auto status = send_stream->Finish();
    
            if (status.ok()) {
                //std::cout << "modpath executed correctly" << std::cout; 
            } else {
                std::cout << "modpath failed with code: " << status.error_code()
                    << " message " << status.error_message() << std::endl;
            }
        }

        std::string GetAllNeighbor(std::string neighbor_ip) {
            gobgpapi::Arguments request;
            request.set_rf(4);
            request.set_name(neighbor_ip);

            ClientContext context;

            gobgpapi::Peer peer;
            grpc::Status status = stub_->GetNeighbor(&context, request, &peer);

            if (status.ok()) {
                gobgpapi::PeerConf peer_conf = peer.conf();
                gobgpapi::PeerInfo peer_info = peer.info();

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
        std::unique_ptr<GobgpApi::Stub> stub_;
};

int main(int argc, char** argv) {
    // We use non absoulte path here and linker will find it fir us
    void* gobgp_library_handle = dlopen("libgobgp.so", RTLD_NOW);

    if (gobgp_library_handle == NULL) {
        printf("Could not load gobgp binary library\n");
        exit(1);
    } 

    dlerror();    /* Clear any existing error */

    /* According to the ISO C standard, casting between function
        pointers and 'void *', as done above, produces undefined results.
        POSIX.1-2003 and POSIX.1-2008 accepted this state of affairs and
        proposed the following workaround:
    */

    serialize_path_dynamic = (serialize_path_dynamic_t)dlsym(gobgp_library_handle, "serialize_path");
    if (serialize_path_dynamic == NULL) {
        printf("Could not load function serialize_path from the dynamic library\n");
        exit(1);
    }

    decode_path_dynamic = (decode_path_dynamic_t)dlsym(gobgp_library_handle, "decode_path");

    if (decode_path_dynamic == NULL) {
        printf("Could not load function decode_path from the dynamic library\n");
        exit(1);
    }

    GrpcClient gobgp_client(grpc::CreateChannel("localhost:8080", grpc::InsecureCredentials()));
 
    //std::string reply = gobgp_client.GetAllNeighbor("213.133.111.200");
    //std::cout << "We received: " << reply << std::endl;

    gobgp_client.AnnounceUnicastPrefix();

    gobgp_client.AnnounceFlowSpecPrefix();

    unsigned int AFI_IP = 1;
    unsigned int SAFI_UNICAST = 1;
    unsigned int SAFI_FLOW_SPEC_UNICAST = 133;

    unsigned int ipv4_unicast_route_family = AFI_IP<<16 | SAFI_UNICAST;
    unsigned int ipv4_flow_spec_route_family = AFI_IP<<16 | SAFI_FLOW_SPEC_UNICAST;   
 
    gobgp_client.GetAllActiveAnnounces(ipv4_unicast_route_family);
    std::cout << std::endl << std::endl;
    gobgp_client.GetAllActiveAnnounces(ipv4_flow_spec_route_family);

    // This code will kill program with segmentation fault
    /*
    dlclose(gobgp_library_handle);

    while (true) {}
    return 0;
    */
}
