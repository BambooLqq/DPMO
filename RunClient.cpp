#include "Client.hpp"

#include <sys/wait.h>
#include <sys/types.h>
#include <getopt.h>

Client* client;
struct Config
{
    uint32_t sock_port_ = 5678;
    uint32_t ib_port_ = 1;
    std::string ib_dev_;
    std::string config_file_;
} config;

static struct option long_options[5]
    = {{.name = "port", .has_arg = 1, .flag = 0, .val = 'p'},
       {.name = "ib-dev", .has_arg = 1, .flag = 0, .val = 'd'},
       {.name = "ib-port", .has_arg = 1, .flag = 0, .val = 'i'},
       {.name = "config-file", .has_arg = 1, .flag = 0, .val = 'c'},
       {.name = NULL, .has_arg = 0, .flag = 0, .val = '\0'}};

/* Catch ctrl-c and destruct. */
void Stop(int signo)
{
    delete client;
    Debug::notifyInfo("Client is terminated, Bye.");
    _exit(0);
}

static void usage(const char* argv0)
{
    fprintf(stdout, "Usage:\n");
    fprintf(stdout, " %s start a server and wait for connection\n", argv0);
    fprintf(stdout, " %s <host> connect to server at <host>\n", argv0);
    fprintf(stdout, "\n");
    fprintf(stdout, "Options:\n");
    fprintf(
        stdout,
        " -p, --port <port> listen on/connect to port <port> (default 0)\n");
    fprintf(
        stdout,
        " -d, --ib-dev <dev> use IB device <dev> (default first device found)\n");
    fprintf(stdout,
            " -i, --ib-port <port> use port <port> of IB device (default 1)\n");
    fprintf(stdout, " -c, --config-file <config-file> config-file path\n");
}

static void print_config(void)
{
    fprintf(stdout, " ------------------------------------------------\n");
    fprintf(stdout, " Device name : \"%s\"\n", config.ib_dev_.c_str());
    fprintf(stdout, " IB port : %u\n", config.ib_port_);
    fprintf(stdout, " TCP port : %u\n", config.sock_port_);
    if (config.config_file_.size() > 0)
        fprintf(stdout, " Config_file: %s\n", config.config_file_.c_str());
    fprintf(stdout, " ------------------------------------------------\n\n");
}

static int ParseArgv(int argc, char** argv)
{
    while (1)
    {
        int c;
        c = getopt_long(argc, argv, "p:d:i:c:", long_options, NULL);
        if (c == -1) break;
        switch (c)
        {
        case 'p': config.sock_port_ = strtoul(optarg, NULL, 0); break;
        case 'd': config.ib_dev_ = strdup(optarg); break;
        case 'i':
            config.ib_port_ = strtoul(optarg, NULL, 0);
            if (config.ib_port_ < 0)
            {
                usage(argv[0]);
                return 1;
            }
            break;
        case 'c':
            if (optarg == NULL)
            {
                usage(argv[0]);
                return 1;
            }
            else
            {
                config.config_file_ = optarg;
            }
            break;
        default: usage(argv[0]); return 1;
        }
    }
    if (optind < argc)
    {
        usage(argv[0]);
        return 1;
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (ParseArgv(argc, argv))
    {
        return 1;
    }
    print_config();
    signal(SIGINT, Stop);
    client = new Client(config.sock_port_, config.config_file_, config.ib_dev_,
                        config.ib_port_);
    client->SendMessageToServer();
    client->SendCreatePool(1234, 0x5678abcd);
    client->SendCreatePool(1234, 0x5678abcd);
    client->SendDeletePool(1245);
    client->SendDeletePool(1234);
    client->SendCreatePool(1234, 0xabcdef12);
    GetRemotePool result;
    if (client->SendFindPool(1234, &result))
    {
        std::cout << "result: ip is : " << result.ip_ << std::endl;
        std::cout << "result: node id is : " << result.node_id_ << std::endl;
        std::cout << "result: va is : " << std::hex << result.virtual_address_
                  << std::oct << std::endl;
    }
    else
    {
        std::cout << "Not Found Poolid " << 1234 << std::endl;
    }

    if (client->SendFindPool(2345, &result))
    {
        std::cout << "result: ip is : " << result.ip_ << std::endl;
        std::cout << "result: node id is : " << result.node_id_ << std::endl;
        std::cout << "result: va is : 0x" << std::hex << result.virtual_address_
                  << std::oct << std::endl;
    }
    else
    {
        std::cout << "Not Found Poolid " << 2345 << std::endl;
    }
    while (1)
    {
    }

    // delete client;
    // while (true)
    // {
    //     getchar();
    //     printf("storage addr = %lx\n", (long)p);
    //     for (int i = 0; i < 12; i++)
    //     {
    //         printf("%c", p[i]);
    //     }
    //     printf("\n");
    // }
}