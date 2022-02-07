
#include "pmsis.h"
#include "bsp/camera/himax.h"
#include "gaplib/jpeg_encoder.h"

static uint8_t spi_tx_buffer[34] __attribute__((aligned(4))); // alignment is a requirement for SPI
static uint8_t spi_rx_cmd_buffer[6] __attribute__((aligned(4)));
static uint8_t spi_rx_buffer[6] __attribute__((aligned(4)));
static uint8_t spi_zeros[32] __attribute__((aligned(4)));
static uint8_t spi_read_status[32] __attribute__((aligned(4)));

// #define DEBUG
// #define JPEG_Q_100

#define DI 0x07
static unsigned char crc8Table[256];     /* 8-bit table */
static uint8_t isRefresh = 0x00;
static uint8_t includeImage = 0x00;
static uint8_t numFeats = 8;

static uint32_t v_lowbit = 0, v_highbit = 0;

#define AT_INPUT_WIDTH    (320)
#define AT_INPUT_HEIGHT   (240)
#define AT_INPUT_COLORS (1)
#define AT_INPUT_SIZE 	(AT_INPUT_WIDTH*AT_INPUT_HEIGHT*AT_INPUT_COLORS)
#define NUM_PIXELS (AT_INPUT_WIDTH*AT_INPUT_HEIGHT)
#define CAMERA_WIDTH    (324)
#define CAMERA_HEIGHT   (244)
#define CAMERA_SIZE   	(CAMERA_HEIGHT*CAMERA_WIDTH)
RT_L2_DATA uint8_t Input_1[CAMERA_WIDTH*CAMERA_HEIGHT] = {0x00};

RT_L2_DATA uint8_t jpegImageBuffer[20*1024] = {0x00};
RT_L2_DATA uint8_t * jpegImage = jpegImageBuffer + 1;
RT_L2_DATA int jpegImageSize = 0;

#define SPI_PACKET_LENGTH (34)
#define SPI_DATA_LENGTH (30)
#define PACKET_LENGTH 726
#define TCP_DATA_LENGTH (726)
RT_L2_DATA uint8_t packet[PACKET_LENGTH];
static uint32_t dataPos = 0;

struct pi_device spi_dev = { 0 };
struct pi_device camera;

struct pi_device gpio_lowbit;
struct pi_device gpio_highbit;
pi_gpio_e gpio_in_lowbit = PI_GPIO_A19_PAD_33_B12;
pi_gpio_e gpio_in_highbit = PI_GPIO_A5_PAD_17_B40;

void crop(uint8_t *img) {
    int ps = 0;
    for (int i =0; i < CAMERA_HEIGHT; i++) {
    	for (int j=0; j < CAMERA_WIDTH; j++) {
    		if (i < AT_INPUT_HEIGHT && j < AT_INPUT_WIDTH) {
    			img[ps] = img[i*CAMERA_WIDTH+j];
    			ps++;
    		}
    	}
    }
}

void capture_img_sync() {
    pi_camera_control(&camera, PI_CAMERA_CMD_START, 0);
    pi_camera_capture(&camera, Input_1, (uint32_t) CAMERA_SIZE);
    pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);
    crop(Input_1);
    /*equalize_histogram(Input_1);*/
}

uint8_t spiSendPacket() {
    uint8_t status = 0x00;
    // uint32_t start_time = rt_time_get_us();
    // spi_read_status[0] = 0x00;
    status = 0x00;

            // pi_spi_send(&spi_dev, spi_cmd_buffer, 8 * 2, PI_SPI_CS_KEEP | PI_SPI_LINES_SINGLE);
    pi_spi_send(&spi_dev, spi_tx_buffer, 8 * 34, PI_SPI_CS_KEEP | PI_SPI_LINES_SINGLE);
            // printf("Packet Send.\n");
    pi_time_wait_us(30);

    do {
        pi_gpio_pin_read(&gpio_lowbit, gpio_in_lowbit, &v_lowbit);
        pi_gpio_pin_read(&gpio_highbit, gpio_in_highbit, &v_highbit);
        status = v_highbit << 1 | v_lowbit;
        // printf("HIGH: %d | LOW: %d | status: %d\n", v_highbit, v_lowbit, status);
        status = v_highbit << 1 | v_lowbit;
        // pi_spi_transfer(&spi_dev, spi_rx_cmd_buffer, spi_rx_buffer, 8 * 6, PI_SPI_CS_KEEP | PI_SPI_LINES_SINGLE);
        // for (size_t j = 0; j < 6; j += 1) {
        //     printf("%x %x\n", spi_rx_cmd_buffer[j], spi_rx_buffer[j]);
        // }
        // status = spi_rx_buffer[2];
        // printf("Status: %d\n", status);
    } while (status == 0x00);

    return status;

// #else
//         pi_task_t wait_task = { 0 };
//         pi_task_block(&wait_task);
//         pi_spi_send_async(&spi_dev, spi_tx_buffer, 8 * sizeof(spi_tx_buffer), PI_SPI_CS_KEEP | PI_SPI_LINES_SINGLE, &wait_task);
//         pi_task_wait_on(&wait_task);
// #endif
//             while (v_lowbit == 1 && v_highbit == 1) {
//                 pi_spi_send(&spi_dev, spi_status_buffer, 8 * 1, PI_SPI_CS_KEEP | PI_SPI_LINES_SINGLE);
// #ifdef DEBUG
//                 for (size_t j = 0; j < 1; j += 1) {
//                     printf("%x ", spi_status_buffer[j]);
//                 }
//                 printf("\n");
// #endif
//                 pi_spi_transfer(&spi_dev, spi_zeros, spi_read_status, 8 * 4, PI_SPI_CS_KEEP | PI_SPI_LINES_SINGLE);
// #ifdef DEBUG
//                 for (size_t j = 0; j < 4; j += 1) {
//                     printf("%x ", spi_read_status[j]);
//                 }
//                 printf("\n");
// #endif
//                 pi_time_wait_us(10);
//             }
            // uint32_t end_time = rt_time_get_us();
            // printf("End Time: %dus\n", end_time - start_time);
            // printf("GPIO Low opened, in val: %d\n", v_lowbit);
            // printf("GPIO HIGH opened, in val: %d\n", v_highbit);
            // pi_time_wait_us(10);

}

static int process_image(int idx)
{

    jpeg_encoder_t enc;
    int pgm_header_size;
    unsigned int width=320, height=240;
    int image_size = width*height;

    // Allocate output jpeg image
    // uint8_t *jpeg_image = pi_l2_malloc(40*1024);
    if (jpegImage == NULL)
        return -1;

    struct jpeg_encoder_conf enc_conf;

    jpeg_encoder_conf_init(&enc_conf);

    // #if RUN_ENCODER_ON_FC
    enc_conf.flags=0x0;
    // #else
    // enc_conf.flags=JPEG_ENCODER_FLAGS_CLUSTER_OFFLOAD;
    // #endif
    enc_conf.width = AT_INPUT_WIDTH;
    enc_conf.height = AT_INPUT_HEIGHT;

    if (jpeg_encoder_open(&enc, &enc_conf)) {
        printf("JPEG Encoder failed.\n");
        return -1;
    }
        

    if (jpeg_encoder_start(&enc)) {
        printf("JPEG Encoder start failed.\n");
        return -1;
    }
        
    // // Get the header so that we can produce full JPEG image
    pi_buffer_t bitstream;
    bitstream.data = jpegImage;
    bitstream.size = image_size;
    uint32_t header_size, footer_size, body_size;

    if (jpeg_encoder_header(&enc, &bitstream, &header_size)) {
        printf("JPEG Encoder header failed.\n");
        return -1;
    }

    // printf("Header size %d\n",header_size);

    // Now get the encoded image
    pi_buffer_t buffer;
    buffer.data = Input_1;
    buffer.size = image_size;
    buffer.width = width;
    buffer.height = height;
    bitstream.data = &jpegImage[header_size];

    pi_perf_conf(1<<PI_PERF_CYCLES);
    pi_perf_start();
    pi_perf_reset();

    if (jpeg_encoder_process(&enc, &buffer, &bitstream, &body_size)) {
        printf("JPEG Encoder encode failed.\n");
        return -1;
    }
      

    pi_perf_stop();

    // Ans finally get the footer
    bitstream.data = &jpegImage[body_size + header_size];
    if (jpeg_encoder_footer(&enc, &bitstream, &footer_size)) {
        printf("JPEG Encoder footer failed.\n");
        return -1;
    }
      
    int bitstream_size = body_size + header_size + footer_size;
    jpegImageSize = bitstream_size;

    // printf("Jpeg Encoding done - size %d - cycles %d\n",bitstream_size,pi_perf_read(PI_PERF_CYCLES));

    // jpeg_encoder_stop(&enc);
    //The stop is closing the cluster, which we do not want to do it
    //TODO add a flag to leave the cluster open
    jpeg_encoder_close(&enc);

    // Now flush the image to the workstation using semi-hosting

    // return bitstream_size;

}

void formTCPPacket(uint8_t * packet, uint8_t * data, int dataLength, uint8_t kind, uint8_t index) {
    uint16_t packetLength = 0;
    uint32_t startPos = TCP_DATA_LENGTH * index;
    if(TCP_DATA_LENGTH * (index + 1) < dataLength) {
        packetLength = TCP_DATA_LENGTH;
    } else {
        packetLength = dataLength - startPos;
    }

    memset(packet, 0, PACKET_LENGTH);

    packet[0] = kind;
    packet[1] = index;
    packet[2] = (uint8_t)(packetLength >> 8) & 0XFF;
    packet[3] = (uint8_t)(packetLength & 0xFF);

    memcpy(packet + 4, data + startPos, packetLength);
}

void formSPIPacket(uint8_t * packet, uint8_t * data, int dataLength, uint8_t index) {
    uint8_t packetLength = 0;
    uint32_t startPos = SPI_DATA_LENGTH * index;
    if(SPI_DATA_LENGTH * (index + 1) < dataLength) {
        packetLength = SPI_DATA_LENGTH;
    } else {
        packetLength = dataLength - startPos;
    }

    memset(spi_tx_buffer, 0, SPI_PACKET_LENGTH);

    spi_tx_buffer[0] = 0x02;
    spi_tx_buffer[1] = 0x00;
    spi_tx_buffer[2] = packetLength;
    spi_tx_buffer[3] = index;
    memcpy(packet + 4, data + startPos, packetLength);
    // for(int j = 4; j < 34; j++) {
    //     spi_tx_buffer[3] ^= spi_tx_buffer[j];
    // }
}

void formPacketForImage(uint8_t * data, int dataLength) {

    uint8_t indexTCP = 0;
    uint8_t indexSPI = 0;
    uint8_t status = 0x00;
    uint8_t totalPacketTCP = 0;
    uint8_t totalPacketSPI = 0;

    // totalPacketTCP = AT_INPUT_SIZE / TCP_DATA_LENGTH + 1;
    totalPacketTCP = dataLength / TCP_DATA_LENGTH + 1;
    while(indexTCP < totalPacketTCP) {
        formTCPPacket(packet, data, dataLength, 0, indexTCP);
        indexTCP++;

        indexSPI = 0;
        totalPacketSPI = TCP_DATA_LENGTH / SPI_DATA_LENGTH + 1;
        // printf("Index: %d %d\n", packet[0], packet[1]);
        // printf("Total SPI Packets: %d\n", totalPacket);
        // for(int i = 0; i < 50; i++) {
        //     printf("%x ", packet[i]);
        // }
        // printf("\n");
        
        while(indexSPI < totalPacketSPI) {
            formSPIPacket(spi_tx_buffer, packet, 730, indexSPI);
            // printf("DataPos: %d, %d\n", spiDataPos, PACKET_LENGTH);
            // for(int i = 0; i < 34; i++) {
            //     printf("%x ", spi_tx_buffer[i]);
            // }
            // printf("\n");
            status = spiSendPacket();
            // printf("Index: %d %d\n\n", spi_tx_buffer[3], status);
            if(status == 0x01) {
                indexSPI += 1;
            } else if(status == 0x02) {
                indexSPI = indexSPI == 0? 0: indexSPI - 1;
            } else if(status == 0x03) {

            }
        }
    }
}

static int open_camera_himax(struct pi_device *device) {
    struct pi_himax_conf cam_conf;
    pi_himax_conf_init(&cam_conf);
    pi_open_from_conf(device, &cam_conf);
    if (pi_camera_open(device))
        return -1;
    pi_camera_control(&camera, PI_CAMERA_CMD_AEG_INIT, 0);
    rt_time_wait_us(1000000); //wait AEG init (takes ~100 ms)
    return 0;
}

/*
* Should be called before any other crc function.  
*/
static void initCrc8() {
    int i,j;
    unsigned char crc;

    for (i=0; i<256; i++) {
        crc = i;
        for (j=0; j<8; j++) {
            crc = (crc << 1) ^ ((crc & 0x80) ? DI : 0);
        }
        crc8Table[i] = crc & 0xFF;
#ifdef DEBUG
        printf("table[%d] = %d (0x%X)\n", i, crc, crc);
#endif
    }
}

/*
* For a byte array whose accumulated crc value is stored in *crc, computes
* resultant crc obtained by appending m to the byte array
*/
uint8_t crc8(uint8_t * byteArray, int start, int end) {   
    uint8_t crc = 0x00;
    for(int i = start; i < end; i++) {
        crc = crc8Table[crc ^ byteArray[i]];
#ifdef DEBUG
        printf("%x:%x ", byteArray[i], crc);
#endif
    }
#ifdef DEBUG
    printf("\n");
#endif
    return crc;
}

void test_spi_master()
{   
    printf("SPI example start\n");

    pi_freq_set(PI_FREQ_DOMAIN_FC, 150*1000*1000);
	pi_freq_set(PI_FREQ_DOMAIN_CL, 150*1000*1000);

    if (open_camera_himax(&camera)) {
        printf("Failed to open camera\n");
        pmsis_exit(-2);
    }

    struct pi_spi_conf spi_conf = { 0 };

    pi_spi_conf_init(&spi_conf);
    spi_conf.max_baudrate = 10 * 1000 * 1000;
    spi_conf.itf = 1; // on Gapuino SPI1 can be used freely
    spi_conf.cs = 0;
    spi_conf.wordsize = PI_SPI_WORDSIZE_8;
    spi_conf.polarity = PI_SPI_POLARITY_0;
    spi_conf.phase = PI_SPI_PHASE_0;

    pi_open_from_conf(&spi_dev, &spi_conf);

    if (pi_spi_open(&spi_dev)) {
        printf("SPI open failed\n");
        pmsis_exit(-1);
    }

    struct pi_gpio_conf gpio_conf;
    pi_gpio_conf_init(&gpio_conf);
    pi_open_from_conf(&gpio_lowbit, &gpio_conf);
    pi_open_from_conf(&gpio_highbit, &gpio_conf);
    if (pi_gpio_open(&gpio_lowbit) || pi_gpio_open(&gpio_highbit)) {
        printf("GPIO open failed\n");
        pmsis_exit(-1);
    }

    pi_gpio_flags_e cfg_flags = PI_GPIO_INPUT; // PI_GPIO_INPUT|PI_GPIO_PULL_DISABLE|PI_GPIO_DRIVE_STRENGTH_LOW;
    pi_gpio_pin_configure(&gpio_lowbit, gpio_in_lowbit, cfg_flags);
    pi_gpio_pin_configure(&gpio_highbit, gpio_in_highbit, cfg_flags);

    // spi_cmd_buffer[0] = 0x02;
    // spi_cmd_buffer[1] = 0x00;

    // spi_tx_buffer[0] = 0x02;
    // spi_tx_buffer[1] = 0x00;

    // spi_status_buffer[0] = 0x04;
    // spi_status_buffer[1] = 0x00;
    // for (int i = 0; i < 32; i++) {
    //     spi_zeros[i] = 0;
    //     spi_read_status[i] = 0;
    // }
    memset(spi_rx_cmd_buffer, 0, 4);
    memset(spi_rx_buffer, 0, 4);
    spi_rx_cmd_buffer[0] = 0x04;

    initCrc8();

    for(int i = 0; i < 20 ;i++) {
        capture_img_sync();
        // uint32_t start = rt_time_get_us();
        // printf("ENCODEING...\n");
        // process_image(0);
        // uint32_t end = rt_time_get_us();
        // printf("Encoded Time: %.3f", (end - start) / 1000);
        // uint8_t totalPacketTCP = (jpegImageSize + 1) / TCP_DATA_LENGTH + 1;
        // jpegImageBuffer[0] = totalPacketTCP;
        // printf("Total Packet: %d\n", jpegImageBuffer[0]);
        // formPacketForImage(jpegImageBuffer, jpegImageSize + 1);
        // pi_time_wait_us(5000 * 1000);
        // jpegImageSize = 0;
        // memset(jpegImageBuffer, 0, sizeof(jpegImageBuffer));
        formPacketForImage(Input_1, AT_INPUT_SIZE);
    }

// //#define SYNC

//     const size_t loops = 1000;
//     for (uint8_t i = 0; i < loops; i += 1) {
        
//         for (size_t j = 4; j < 34; j += 1) {
            
//             spi_tx_buffer[j] = 0x00 + j + i;
//         }
//         spi_tx_buffer[2] = 30;
//         // spi_tx_buffer[3] = crc8(spi_tx_buffer, 4, 34);
//         spi_tx_buffer[3] = 0x00;
//         for(int j = 4; j < 34; j++) {
//             spi_tx_buffer[3] ^= spi_tx_buffer[j];
//         }

// #ifdef DEBUG
//         for(int j = 0; j < 32; j++) {
//             printf("%x ", spi_tx_buffer[j]);
//         }
// #endif
//         // printf("\n");
//         // printf("SPI send (%i/%i), size: %i\n", i + 1, loops, sizeof(spi_tx_buffer));

//         // use pi_spi_send to only send data, pi_spi_receive to only receive data and pi_spi_transfer to send and receive data

// // #ifdef SYNC
//         do {
//             uint32_t start_time = rt_time_get_us();
//             // spi_read_status[0] = 0x00;
//             v_lowbit = 0;
//             v_highbit = 0;

//             // pi_spi_send(&spi_dev, spi_cmd_buffer, 8 * 2, PI_SPI_CS_KEEP | PI_SPI_LINES_SINGLE);
//             pi_spi_send(&spi_dev, spi_tx_buffer, 8 * 34, PI_SPI_CS_KEEP | PI_SPI_LINES_SINGLE);
//             // pi_time_wait_us(10);

//             do {
//                 pi_gpio_pin_read(&gpio_lowbit, gpio_in_lowbit, &v_lowbit);
//                 pi_gpio_pin_read(&gpio_highbit, gpio_in_highbit, &v_highbit);
//             } while (v_lowbit == 0 && v_highbit == 0);
// // #else
// //         pi_task_t wait_task = { 0 };
// //         pi_task_block(&wait_task);
// //         pi_spi_send_async(&spi_dev, spi_tx_buffer, 8 * sizeof(spi_tx_buffer), PI_SPI_CS_KEEP | PI_SPI_LINES_SINGLE, &wait_task);
// //         pi_task_wait_on(&wait_task);
// // #endif
// //             while (v_lowbit == 1 && v_highbit == 1) {
// //                 pi_spi_send(&spi_dev, spi_status_buffer, 8 * 1, PI_SPI_CS_KEEP | PI_SPI_LINES_SINGLE);
// // #ifdef DEBUG
// //                 for (size_t j = 0; j < 1; j += 1) {
// //                     printf("%x ", spi_status_buffer[j]);
// //                 }
// //                 printf("\n");
// // #endif
// //                 pi_spi_transfer(&spi_dev, spi_zeros, spi_read_status, 8 * 4, PI_SPI_CS_KEEP | PI_SPI_LINES_SINGLE);
// // #ifdef DEBUG
// //                 for (size_t j = 0; j < 4; j += 1) {
// //                     printf("%x ", spi_read_status[j]);
// //                 }
// //                 printf("\n");
// // #endif
// //                 pi_time_wait_us(10);
// //             }
//             uint32_t end_time = rt_time_get_us();
//             // printf("End Time: %dus\n", end_time - start_time);
//             // printf("GPIO Low opened, in val: %d\n", v_lowbit);
//             // printf("GPIO HIGH opened, in val: %d\n", v_highbit);
//             // pi_time_wait_us(10);
//         } while ((v_lowbit == 0 && v_highbit == 1));

//         if(spi_read_status[0] == 0x03) {

//         }
//     }

    printf("SPI example end\n");

    pmsis_exit(0);
}

int main(void)
{
    printf("\n\n\t *** PMSIS SPI Master ***\n\n");
    return pmsis_kickoff((void *) test_spi_master);
}
