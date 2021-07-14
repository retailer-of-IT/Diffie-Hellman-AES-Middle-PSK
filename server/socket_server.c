#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "aes_server.h"
#include "DH.h"

#define MAX 1024

void exchange_dh_key(int sockfd, mpz_t s);
void msg_to_pt(char *plain_text);
void send_encryp_msg(int sockfd, unsigned char *cipher_text);
void recv_encryp_msg(int sockfd, unsigned char *cipher_text);

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("USAGE: ./server ListenPort\nExample: ./server 8888");
        return 0;
    }
    int sockfd, connfd, len;
    struct sockaddr_in serv_addr, cli;

    // create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        printf("Socket Failed!\n");
        exit(-1);
    }
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    if ((bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) != 0)
    {
        printf("Bind Failed\n");
        exit(-1);
    }

    if ((listen(sockfd, 5)) != 0)
    {
        printf("Listen Failed!\n");
        exit(-1);
    }
    len = sizeof(cli);

    connfd = accept(sockfd, (struct sockaddr *)&cli, &len);
    if (connfd < 0)
    {
        printf("Scccept Failed!\n");
        exit(-1);
    }

    // Function for chatting between client and server
    mpz_t dh_s;
    mpz_init(dh_s);
    exchange_dh_key(connfd, dh_s);

    // 声明AES加密解密所需要的变量
    unsigned char plain_text[MAX], key[32];
    unsigned char cipher_text[MAX + EVP_MAX_BLOCK_LENGTH], tag[100];
    unsigned char pt[MAX + EVP_MAX_BLOCK_LENGTH];
    unsigned char iv[16];
    unsigned char aad[16] = "abcdefghijklmnop";
    int rst;
    mpz_get_str(key, 16, dh_s); // 将dh_s写入key
    gmp_printf("DH协议商讨出的密钥为：%Zd\n", dh_s);
    mpz_clear(dh_s); // 清除dh_s
    if (!PKCS5_PBKDF2_HMAC_SHA1(key, strlen(key), NULL, 0, 1000, 32, key))
    {
        printf("AES密钥生成错误！\n");
        exit(-1);
    }
    printf("处理后的AES密钥为：%s\n", key);

    while (!RAND_bytes(iv, sizeof(iv)))
        ;

    while (1)
    {
        // 接收客户端发送的密文
        bzero(cipher_text, MAX + EVP_MAX_BLOCK_LENGTH);
        read(connfd, cipher_text, sizeof(cipher_text));
        printf("接收到的密文：%s\n", cipher_text);

        // 解密
        bzero(pt, MAX + EVP_MAX_BLOCK_LENGTH);
        int rst = decrypt(cipher_text, sizeof(cipher_text), aad,
                          sizeof(aad), tag, key, iv, pt);
        if (rst > 0)
        {
            pt[rst] = '\0';
            printf("解密后的明文：%s\n", pt);
        }
        else
            printf("解密失败！\n");

        // 向客户端发送数据
        bzero(plain_text, MAX);
        printf("要发送的明文: ");
        scanf("%s", plain_text);

        // 加密明文
        bzero(cipher_text, MAX + EVP_MAX_BLOCK_LENGTH);
        encrypt(plain_text, strlen(plain_text), aad,
                sizeof(aad), key, iv, cipher_text, tag);
        write(sockfd, cipher_text, sizeof(cipher_text)); // 发送密文
        printf("发送的密文：%s\n\n\n", cipher_text);
    }

    // After chatting close the socket
    close(sockfd);
    return 0;
}

void exchange_dh_key(int sockfd, mpz_t s)
{
    DH_key server_dh_key;
    mpz_t client_pub_key; // publick key(A) from client
    char buf[MAX];
    mpz_inits(server_dh_key.p, server_dh_key.g, server_dh_key.pri_key,
              server_dh_key.pub_key, server_dh_key.s, client_pub_key, NULL);
    mpz_set_ui(server_dh_key.g, (unsigned long int)5); // g = 5
    // recv p form client
    bzero(buf, MAX);
    read(sockfd, buf, sizeof(buf));
    mpz_set_str(server_dh_key.p, buf, 16);
    // gmp_printf("p = %Zd\n", server_dh_key.p);

    // generate private key(b) of server
    generate_pri_key(server_dh_key.pri_key);
    // gmp_printf("b = %Zd\n", server_dh_key.pri_key);
    // calc the public key B of server
    mpz_powm(server_dh_key.pub_key, server_dh_key.g, server_dh_key.pri_key,
             server_dh_key.p);

    // send public key(B) of server to client
    bzero(buf, MAX);
    mpz_get_str(buf, 16, server_dh_key.pub_key);
    write(sockfd, buf, sizeof(buf));
    // gmp_printf("B = %Zd\n", server_dh_key.pub_key);

    // recv A form server
    bzero(buf, MAX);
    read(sockfd, buf, sizeof(buf));
    mpz_set_str(client_pub_key, buf, 16);
    // gmp_printf("A = %Zd\n", client_pub_key);

    // calc key s
    mpz_powm(server_dh_key.s, client_pub_key, server_dh_key.pri_key,
             server_dh_key.p);
    // gmp_printf("s = %Zd\n", server_dh_key.s);
    mpz_set(s, server_dh_key.s);

    mpz_clears(server_dh_key.p, server_dh_key.g, server_dh_key.pri_key,
               server_dh_key.pub_key, server_dh_key.s, client_pub_key, NULL);
}