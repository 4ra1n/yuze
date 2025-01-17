#include "portTransmit.h"

// -m yuze_tran -s 7878 -d 127.0.0.1 -e 9999 将7878 端口收到的socks代理请求转交给127.0.0.1打开正向监听的主机
// Forword data to remote host ,remote host should be Forward connection host
int yuze_tran(int refPort, char* destHost, int destPort) {
    SOCKET server_sock;
    printf("[+] Port forwart to RemoteHost 0.0.0.0:%d <--[yuze]--> %s:%d\n", refPort, destHost, destPort);

    server_sock = socket_initListenServer(refPort, 500);
    for (;;) {
        fd_set readset;
        struct timeval tv = { 60,0 };
        FD_ZERO(&readset);
        FD_SET(server_sock, &readset);

        int selectRet = select(server_sock + 1, &readset, NULL, NULL, &tv);
        if (selectRet < 0)
        {
            puts("[-] yuze_tran -》 Select error");
            break;
        }
        else if (selectRet == 0)
        {
            puts("[-] yuze_tran -》 Select time out");
            continue;
        }
        else if (selectRet) {
            if (FD_ISSET(server_sock, &readset)) {
                SOCKET clnt_sock = socket_acceptClient(server_sock);
                if (clnt_sock == SOCKET_ERROR) {
                    //puts("[+] Client Connected");
                }

                SOCKET dest_sock = socket_connect(destHost, destPort);
                if (dest_sock == SOCKET_ERROR) {
                    //puts("[-] RemoteHost Connected closed");
                    closesocket(dest_sock);
                    exit(EXIT_FAILURE);
                }
                else {
                    //puts("[+] Connect RemoteHost Succeed");
                }
                tunnel_sock_to_sock(clnt_sock, dest_sock);
            }
        }
    }
}


// -m yuze_slave -r 1.1.1.1 -s 8888 -d 2.2.2.3 -e 9999
// 在 2.2.2.2 上，通过工具的 yuze_slave 方式，打通1.1.1.1:8888 和 2.2.2.3:9999 之间的通讯隧道
int yuze_slave(char* ref_host, int ref_port, char* dest_host, int dest_port) {
    int readSize;
    char newSocketNotice[RSOCKET_SERVER_NOTICE_LEN];
    slaveStructalias slaveConfig;
    printf("[+] yuze_slave %s:%d <--[yuze]--> %s:%d\n", ref_host, ref_port, dest_host, dest_port);

    SOCKET control_socket = rsocksClient_init_ControlSocket(refHost, refPort);
    if (control_socket == SOCKET_ERROR) {
        printf("[-] Can not get Control_socket connect\n");
        closesocket(control_socket);
        return -1;
    }

    for (;;) {
        fd_set readset;
        struct timeval tv = { 60,0 }; // set select func tiemout time 
        int retval;

        FD_ZERO(&readset);
        FD_SET(control_socket, &readset);

        int maxfd = control_socket + 1;
        retval = select(maxfd, &readset, NULL, NULL, &tv);
        if (retval == -1) {
            perror("[-] yuze_slave func select() error");
            exit(1);
        }
        else if (retval == 0)
        {
            puts("[-] wait time-out!");
        }
        else {
            if (FD_ISSET(control_socket, &readset)) {
                readSize = socket_recv(control_socket, newSocketNotice, RSOCKET_SERVER_NOTICE_LEN);
                if (readSize == RSOCKET_SERVER_NOTICE_LEN && newSocketNotice[0] == True && newSocketNotice[1] == NEW_PROXY_SOCKET) {
                    strncpy(slaveConfig.refHost, ref_host, 300);
                    slaveConfig.refport = ref_port;
                    strncpy(slaveConfig.destHost, dest_host, 300);
                    slaveConfig.destport = dest_port;

                    slaveConfig.tunnel_id = newSocketNotice[2] - '0' + 48;
                    CreateThread(0, 0, (LPTHREAD_START_ROUTINE)yuzeslave_build_tunnel, &slaveConfig, 0, 0);
                }
                else
                {
                    printf("[-] Error Happended on %d\n", readSize);
                    yuze_slave(ref_host, ref_port, dest_host, dest_port);
                    return -1;
                }
            }
        }
    }
}

int yuzeslave_build_tunnel(slaveStructalias* slavesConfig) {
    char rhost[300], dhost[300];
    char char_tunnel_id;
    char NewSocketNoticebuff[RSOCKET_SERVER_NOTICE_LEN];

    strncpy(rhost, slavesConfig->refHost, 300);
    int rport = slavesConfig->refport;
    strncpy(dhost, slavesConfig->destHost, 300);
    int dport = slavesConfig->destport;
    char tunnel_id = slavesConfig->tunnel_id;

    SOCKET proxySocket = socket_connect(rhost, rport);
    FillinSocketbuff(NewSocketNoticebuff);
    NewSocketNoticebuff[0] = True;
    NewSocketNoticebuff[1] = NEW_PROXY_SOCKET;
    NewSocketNoticebuff[2] = tunnel_id;
    int sendlen = socket_send(proxySocket, NewSocketNoticebuff, RSOCKET_SERVER_NOTICE_LEN);
    if (sendlen != RSOCKET_SERVER_NOTICE_LEN) {
        puts("[-] ");
    }

    SOCKET dest_socket = socket_connect(dhost, dport);
    if (dest_socket != SOCKET_ERROR) { // Check the sockets proto version
        tunnel_sock_to_sock(proxySocket, dest_socket);
    }
}

int yuze_listen(int clntPort, int rsocksPort) {
    printf("[+] Port Forwarding 0.0.0.0:%d <--[yuze]--> 0.0.0.0:%d\n", clntPort, rsocksPort);

    if (CreateThread(0, 0, (LPTHREAD_START_ROUTINE)yuzelisten_create_clntPortServer, &clntPort, 0, 0) == NULL) {
        printf("Error: --> %d start server\n", clntPort);
        return -1;
    }

    if (CreateThread(0, 0, (LPTHREAD_START_ROUTINE)yuzelisten_create_rsocksPortServer, &rsocksPort, 0, 0) == NULL) {
        printf("Error: --> %d start server\n", rsocksPort);
        return -1;
    }

    //to prevent the Host process exit
    for (;;) {
        Sleep(1000);
    }
    return 1;
}

// for listen socket connect from client 
int yuzelisten_create_clntPortServer(int* p_lstPort) {
    SOCKET serv_sock;
    int lstPort = *p_lstPort;
    char sendbuf[RSOCKET_SERVER_NOTICE_LEN], recvbuf[RSOCKET_SERVER_NOTICE_LEN];
    int sendlen, recvlen;

    serv_sock = socket_initListenServer(lstPort, 500);
    if (serv_sock == INVALID_SOCKET)
    {
        printf("[-] Error: --> Unable to start server on port %d.\n", lstPort);
        exit(1);
    }
    printf("[+] Create_socks_server 0.0.0.0:%d <--[yuze]--> Client socks server\n", lstPort);

    while (True) {
        struct sockaddr_in sa;
        int slen = sizeof(sa);
        SOCKET s = accept(serv_sock, (struct sockaddr*)&sa, &slen);
        if (s == SOCKET_ERROR) {
            if (errno == EINTR)
                continue; /* Try again. */
            else
                break;
        }
        FillinSocketbuff(recvbuf);
        int tunnel_id = tunnel_set_refSocket_and_get_enable_id(s);
        sendbuf[0] = True;
        sendbuf[1] = NEW_PROXY_SOCKET;
        sendbuf[2] = tunnel_id;
        sendlen = socket_send(control_socket, sendbuf, RSOCKET_SERVER_NOTICE_LEN);
        if (sendlen != RSOCKET_SERVER_NOTICE_LEN) {
            puts("[-] No Control_socket");
            return -1;
        }
    }
}

// for listen socket connect from the reverse connect host
int yuzelisten_create_rsocksPortServer(int* p_rlstPort) {
    SOCKET serv_sock;
    int rlstPort = *p_rlstPort;
    char sendbuf[RSOCKET_SERVER_NOTICE_LEN], recvbuf[RSOCKET_SERVER_NOTICE_LEN];
    int sendlen, recvlen;

    serv_sock = socket_initListenServer(rlstPort, SOCKET_LISTEN_BACKLOG);
    if (serv_sock == INVALID_SOCKET)
    {
        printf("[-] Error: --> Unable to start server on port %d.\n", rlstPort);
        exit(1);
    }
    printf("[+] Create_socks_server 0.0.0.0:%d <--[yuze]--> rserver socks server\n", rlstPort);

    while (True) {
        struct sockaddr_in sa;
        int slen = sizeof(sa);
        SOCKET s = accept(serv_sock, (struct sockaddr*)&sa, &slen);
        if (s == -1) {
            if (errno == EINTR)
                continue; /* Try again. */
            else
                break;
        }

        int recvlen = socket_recv(s, recvbuf, RSOCKET_SERVER_NOTICE_LEN);
        if (recvlen != RSOCKET_SERVER_NOTICE_LEN) {
            puts("[-] rsocks_client send data Error");
            return -1;
        }
        if (recvbuf[1] == CONTROL_SOCKET) {
            puts("[+] Come to a control Socket");
            sendbuf[1] = CONFIRM_CONTROL_SOCKET;
            sendlen = socket_send(s, sendbuf, RSOCKET_SERVER_NOTICE_LEN);
            control_socket = s;
        }
        else if (recvbuf[1] == NEW_PROXY_SOCKET)
        {
            //int recvlen = socket_recv(s, recvbuf, RSOCKET_SERVER_NOTICE_LEN);
            puts("[+] Come to a new proxy Socket");
            int tunnel_id = recvbuf[2] - '0' + 48;
            tunnel_set_destSocket_and_run_it(tunnel_id, s);
        }
    }
    return -1;
}
