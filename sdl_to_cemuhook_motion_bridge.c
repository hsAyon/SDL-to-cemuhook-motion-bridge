#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>
#include <stdbool.h>
#include <fcntl.h>
#include <time.h>

#define PORT 26760

volatile sig_atomic_t keep_running = 1;
void handle_sigint(int signum) { keep_running = 0; }

struct DSUHeader {
    char magic[4];          
    uint16_t protocol;      
    uint16_t length;        
    uint32_t crc;           
    uint32_t id;            
    uint32_t type;          
} __attribute__((packed));

struct CemuhookMotionPacket {
    struct DSUHeader header; 
    uint8_t slot;            
    uint8_t slot_state;      
    uint8_t device_model;    
    uint8_t connection_type; 
    uint8_t mac_address[6];  
    uint8_t battery_status;  
    uint8_t motion_enabled;  
    
    uint32_t packet_counter; 
    uint8_t digital_buttons_1; 
    uint8_t digital_buttons_2; 
    uint8_t home_button;       
    uint8_t touch_button;      
    uint8_t left_stick_x;    
    uint8_t left_stick_y;    
    uint8_t right_stick_x;   
    uint8_t right_stick_y;   
    
    uint8_t analog_pressure_dpad[4];   
    uint8_t analog_pressure_buttons[8];
    uint8_t configuration_padding[12]; 
    
    uint64_t timestamp;      

    float acc_x;             
    float acc_y;             
    float acc_z;             
    
    float gyro_p;            
    float gyro_y;            
    float gyro_r;            
} __attribute__((packed));

uint32_t compute_crc32(const unsigned char *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else crc >>= 1;
        }
    }
    return ~crc;
}

void print_timestamped_log(const char *status_type, const char *message) {
    time_t now;
    time(&now);
    struct tm *local = localtime(&now);
    printf("\n[%02d:%02d:%02d] [%s] %s\n", local->tm_hour, local->tm_min, local->tm_sec, status_type, message);
}

int main(int argc, char* argv[]) {
    signal(SIGINT, handle_sigint);
    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_SENSOR)) {
        printf("SDL Initialization Failed: %s\n", SDL_GetError());
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Socket Binding Failed on port %d.\n", PORT);
        return 1;
    }
    
    fcntl(sock, F_SETFL, O_NONBLOCK);

    unsigned char rx_buffer[128];
    struct CemuhookMotionPacket tx_packet;
    
    SDL_Gamepad *gamepad = NULL;
    SDL_JoystickID current_instance_id = 0;
    uint32_t packet_counter = 0;
    bool client_connected = false;
    uint64_t last_log_time = 0;

    printf("=========================================================\n");
    printf("     SDL TO DSU/CEMUHOOK MOTION BRIDGE (PORT: %d)       \n", PORT);
    printf("=========================================================\n");
    printf("[STATUS] Server online and initialized cleanly.\n");
    printf("[SEARCH] Awaiting controller connection...\n");

    while (keep_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                if (!gamepad) {
                    gamepad = SDL_OpenGamepad(event.gdevice.which);
                    if (gamepad) {
                        current_instance_id = event.gdevice.which;
                        char msg[256];
                        snprintf(msg, sizeof(msg), "Controller connected: %s", SDL_GetGamepadName(gamepad));
                        print_timestamped_log("HARDWARE", msg);
                        
                        SDL_SetGamepadSensorEnabled(gamepad, SDL_SENSOR_ACCEL, true);
                        SDL_SetGamepadSensorEnabled(gamepad, SDL_SENSOR_GYRO, true);
                    }
                }
            }
            
            if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                if (gamepad && event.gdevice.which == current_instance_id) {
                    print_timestamped_log("HARDWARE", "Controller disconnected.");
                    SDL_CloseGamepad(gamepad);
                    gamepad = NULL;
                    current_instance_id = 0;
                    client_connected = false; 
                    printf("[SEARCH] Going back to search mode. Awaiting device link...\n");
                }
            }
        }

        if (!gamepad) {
            usleep(10000); 
            continue;
        }

        int rx_bytes = recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr*)&client_addr, &client_len);
        if (rx_bytes >= 20) {
            struct DSUHeader *req_header = (struct DSUHeader*)rx_buffer;
            
            if (req_header->type == 0x00100001) {
                int32_t ports_requested = *(int32_t*)(rx_buffer + 20);
                for (int p = 0; p < ports_requested && p < 4; p++) {
                    uint8_t target_slot = rx_buffer[24 + p];
                    unsigned char info_buffer[32];
                    memset(info_buffer, 0, sizeof(info_buffer));
                    struct DSUHeader *res_header = (struct DSUHeader*)info_buffer;
                    
                    memcpy(res_header->magic, "DSUS", 4);
                    res_header->protocol = 0x03E9;       
                    res_header->length = 0x0010;         
                    res_header->id = 0xC9846C0B;         
                    res_header->type = 0x00100001;       

                    info_buffer[16] = target_slot; 
                    info_buffer[17] = (target_slot == 0) ? 0x02 : 0x00; 
                    info_buffer[18] = 0x02; 
                    info_buffer[19] = 0x02; 
                    memcpy(info_buffer + 20, "\x02\x00\x00\x00\x00\x00\x01\x00", 8); 

                    res_header->crc = 0;
                    res_header->crc = compute_crc32(info_buffer, 32);
                    sendto(sock, info_buffer, 32, 0, (struct sockaddr*)&client_addr, client_len);
                }
            }
            if (req_header->type == 0x00100002) {
                if (!client_connected) {
                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Data stream active with host client [%s]", ip_str);
                    print_timestamped_log("NETWORK", msg);
                }
                client_connected = true;
            }
        }

        float accel_data[3] = {0}, gyro_data[3] = {0};
        
        if (!SDL_GetGamepadSensorData(gamepad, SDL_SENSOR_ACCEL, accel_data, 3) ||
            !SDL_GetGamepadSensorData(gamepad, SDL_SENSOR_GYRO, gyro_data, 3)) {
            usleep(1000);
            continue;
        }

        if (client_connected) {
            memset(&tx_packet, 0, sizeof(struct CemuhookMotionPacket));

            memcpy(tx_packet.header.magic, "DSUS", 4);
            tx_packet.header.protocol = 0x03E9;
            tx_packet.header.length = 0x0054;   
            tx_packet.header.id = 0xC9846C0B;   
            tx_packet.header.type = 0x00100002; 

            tx_packet.slot = 0x00;                           
            tx_packet.slot_state = 0x02;        
            tx_packet.device_model = 0x02;      
            tx_packet.connection_type = 0x02;   
            tx_packet.mac_address[0] = 0x02;    
            tx_packet.battery_status = 0x01;    
            tx_packet.motion_enabled = 0x01;    

            tx_packet.packet_counter = packet_counter++;

            tx_packet.digital_buttons_1 = 0x00;
            tx_packet.digital_buttons_2 = 0x00;
            tx_packet.home_button = 0x00;
            tx_packet.touch_button = 0x00;

            tx_packet.left_stick_x = 0x00;
            tx_packet.left_stick_y = 0xFF;
            tx_packet.right_stick_x = 0x00;
            tx_packet.right_stick_y = 0xFF;

            tx_packet.timestamp = SDL_GetTicks() * 1000;

            tx_packet.acc_x = accel_data[0]; 
            tx_packet.acc_y = accel_data[1]; 
            tx_packet.acc_z = accel_data[2];

            tx_packet.gyro_p = gyro_data[0];
            tx_packet.gyro_y = gyro_data[1];
            tx_packet.gyro_r = gyro_data[2];

            tx_packet.header.crc = 0;
            tx_packet.header.crc = compute_crc32((const unsigned char*)&tx_packet, sizeof(struct CemuhookMotionPacket));

            sendto(sock, &tx_packet, sizeof(struct CemuhookMotionPacket), 0, (struct sockaddr*)&client_addr, client_len);
        }

        uint64_t current_time = SDL_GetTicks();
        if (current_time - last_log_time >= 33) {
            printf("\r[SENSORS] Accel: X:%5.2f Y:%5.2f Z:%5.2f | Gyro: P:%5.2f Y:%5.2f R:%5.2f | Link: %s", 
                   accel_data[0], accel_data[1], accel_data[2],
                   gyro_data[0], gyro_data[1], gyro_data[2],
                   client_connected ? "TRANSMITTING" : "STANDBY");
            fflush(stdout);
            last_log_time = current_time;
        }

        usleep(1000); 
    }

    printf("\n\n[SHUTDOWN] Terminating bridge cleanly...\n");
    if (gamepad) SDL_CloseGamepad(gamepad);
    close(sock);
    SDL_Quit();
    return 0;
}
