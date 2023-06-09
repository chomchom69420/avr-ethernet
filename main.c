/*************************************************************************
* main.c
*
* This is template code for the SER486 final exam.  Students may use this
* as a starting point their final project.
*/
#include "config.h"
#include "delay.h"
#include "dhcp.h"
#include "led.h"
#include "log.h"
#include "rtc.h"
#include "spi.h"
#include "uart.h"
#include "vpd.h"
#include "temp.h"
#include "socket.h"
#include "alarm.h"
#include "wdt.h"
#include "tempfsm.h"
#include "eeprom.h"
#include "ntp.h"
#include "w51.h"
#include "signature.h"
#include "process_packet.h"

#define HTTP_PORT       8080	/* TCP port for HTTP */
#define SERVER_SOCKET   0

int current_temperature = 75;

enum HTTP_parse_fsm {REQ_LINE, HEADER, BODY, ERR, PROCESS, SEND_ERR, SEND_RES} parse_state;

/*
*   Request Mode
*   GET 0
*   PUT 1
*   DELETE 2
*   none -1
*
*   this would help us in the processing
*/

int reqMode;

/*
*   Change mode -- indicates which variable to change
*   tcrit_hi    0
*   twarn_hi    1
*   twarn_lo    2
*   tcrit_lo    3
*   none        -1
*
*   this would help us in the processing step
*/

int changeMode;

/*
*   Resource mode -- indicates which resource is being referred to
*   /device             0
*   /device/config      1
*   /device/log         2
*   none                -1
*
*   this would help us in the processing step
*/

int resMode;

/*
*   Reset Mode
*   true    1
*   false   0
*/
int resetMode;

int changeVal;

void http_parse_init();
void http_parse_update();

unsigned char buf[200];

unsigned char* local_ip;
unsigned char message_ip[4];

int main(void)
{
	/* Initialize the hardware devices
	 * uart
	 * led
	 * vpd
	 * config
     * log
     * rtc
     * spi
     * temp sensor
	 * W51 Ethernet controller
     * tempfsm
     */
     uart_init();
     led_init();
     vpd_init();
     config_init();
     log_init();
     rtc_init();
     spi_init();
     temp_init();
     W5x_init();
     tempfsm_init();

     //Initialize http parse fsm
     http_parse_init();

    /* sign the assignment
    * Asurite is the first part of your asu email (before the @asu.edu
    */
    signature_set("Manish","Mani","mmysorer");

    /* configure the W51xx ethernet controller prior to DHCP */
    unsigned char blank_addr[] = {0,0,0,0};
    W5x_config(vpd.mac_address, blank_addr, blank_addr, blank_addr);

    /* loop until a dhcp address has been gotten */
    while (!dhcp_start(vpd.mac_address, 60000UL, 4000UL)) {}
    local_ip = dhcp_getLocalIp();
    uart_writestr("local ip: ");uart_writeip(local_ip);

    /* configure the MAC, TCP, subnet and gateway addresses for the Ethernet controller*/
    W5x_config(vpd.mac_address, dhcp_getLocalIp(), dhcp_getGatewayIp(), dhcp_getSubnetMask());

	/* add a log record for EVENT_TIMESET prior to synchronizing with network time */
	log_add_record(EVENT_TIMESET);

    /* synchronize with network time */
    ntp_sync_network_time(5);

    /* add a log record for EVENT_NEWTIME now that time has been synchronized */
    log_add_record(EVENT_NEWTIME);

    /* start the watchdog timer */
    wdt_init();

    /* log the EVENT STARTUP and send and ALARM to the Master Controller */
    log_add_record(EVENT_STARTUP);
    alarm_send(EVENT_STARTUP);

    /* request start of test if 'T' key pressed - You may run up to 3 tests per
     * day.  Results will be e-mailed to you at the address asurite@asu.edu
     */
    check_for_test_start();

    /* start the first temperature reading and wait 5 seconds before reading again
    * this long delay ensures that the temperature spike during startup does not
    * trigger any false alarms.
    */


    while (1) {
        /* reset  the watchdog timer every loop */
        wdt_reset();

        /* update the LED blink state */
        led_update();

        /* if the temperature sensor delay is done, update the current temperature
        * from the temperature sensor, update the temperature sensor finite state
        * machine (which provides hysteresis) and send any temperature sensor
        * alarms (from FSM update).
        */
        if(delay_isdone(1))
        {
            /* read the temperature sensor */
            current_temperature = temp_get();


            /* update the temperature fsm and send any alarms associated with it */
            tempfsm_update(current_temperature,config.hi_alarm,config.hi_warn,config.lo_alarm,config.lo_warn);

            /* restart the temperature sensor delay to trigger in 1 second */
            temp_start();

        }

        /*if the server socket is closed */
        if(socket_is_closed(SERVER_SOCKET))
        {
            /* if socket is closed, open it in passive (listen) mode */
            socket_open(SERVER_SOCKET, HTTP_PORT);
            socket_listen(SERVER_SOCKET);

        }

        /* if there is input to process */
        else if(socket_received_line(SERVER_SOCKET) || parse_state == PROCESS || parse_state == ERR)
        {
            /* parse and process any pending commands */

            //Read the HTTP request format

            /*
            if(socket_recv(SERVER_SOCKET, buf, 200))
            {
                uart_writestr((char*)buf);
                uart_writestr("\r\n");
                socket_flush_line(SERVER_SOCKET);
            }
            */


            http_parse_update();
        }
        /* otherwise... */
        else {
          /* update any pending log write backs */
            if (!eeprom_isbusy()) log_update();
          /* update any pending config write backs */
            if (!eeprom_isbusy()) config_update();

       }
    }
	return 0;
}

void http_parse_init()
{
    parse_state = REQ_LINE;
}

void http_parse_update()
{
    //this function implements the https parse fsm
    switch(parse_state)
    {
    case REQ_LINE:

        //uart_writestr("In request line section...\r\n");

        if(socket_recv_compare(SERVER_SOCKET, "GET "))
        {
            reqMode=0;
            uart_writestr("GET mode\r\n");
        }
        else if(socket_recv_compare(SERVER_SOCKET, "PUT "))
        {
            reqMode=1;
            uart_writestr("PUT mode\r\n");
        }
        else if(socket_recv_compare(SERVER_SOCKET, "DELETE "))
        {
            reqMode=2;
            uart_writestr("DELETE mode\r\n");
        }
        else
        {
            reqMode=-1;
            parse_state=ERR;
            break;
        }

        if(!socket_recv_compare(SERVER_SOCKET, "/device"))
        {
            uart_writestr("/device not present\r\n");
            parse_state = ERR;
            break;
        }

        //Check proper resource
        if(socket_recv_compare(SERVER_SOCKET, " ") || socket_recv_compare(SERVER_SOCKET, "?reset="))
        {
            //if mode is GET or PUT and resource is /device then cool else raise error
            if(reqMode!=0 && reqMode!=1)
            {
                uart_writestr("Request mode not matching resource\r\n");
                parse_state = ERR;
                break;
            }
            resMode=0;
        }
        else if(socket_recv_compare(SERVER_SOCKET, "/config"))
        {
            if(reqMode!=1)
            {
                uart_writestr("Request mode not matching resource\r\n");
                //Error
                parse_state = ERR;
                break;
            }
            resMode=1;
        }
        else if(socket_recv_compare(SERVER_SOCKET, "/log"))
        {
            if(reqMode!=2)
            {
                uart_writestr("Request mode not matching resource\r\n");
                //Error
                parse_state = ERR;
                break;
            }
            resMode=2;
        }
        else
        {
            uart_writestr("Invalid request mode\r\n");
            resMode=-1;
            parse_state = ERR;
            break;
        }

        if(reqMode==1 && resMode==0)
        {
            //Device has to be reset
            if(socket_recv_compare(SERVER_SOCKET, "\"true\""))  resetMode=1;
            else if(socket_recv_compare(SERVER_SOCKET, "\"false\""))  resetMode=0;
            else
            {
                parse_state = ERR;
                break;
            }
        }

        //For PUT check proper variable being used or not
        if(reqMode==1 && resMode==1)
        {
            if(socket_recv_compare(SERVER_SOCKET, "?tcrit_hi="))        changeMode=0;
            else if(socket_recv_compare(SERVER_SOCKET, "?twarn_hi="))   changeMode=1;
            else if(socket_recv_compare(SERVER_SOCKET, "?twarn_lo="))   changeMode=2;
            else if(socket_recv_compare(SERVER_SOCKET, "?tcrit_lo="))   changeMode=3;
            else
            {
                changeMode=-1;
                parse_state = ERR;
                break;
            }

            //store new value
            if(!socket_recv_int(SERVER_SOCKET, &changeVal))
            {
                changeMode=-1;
                parse_state = ERR;
                break;
            }
        }

        if(reqMode==0)
        {
            //Check proper HTTP version
            if(!socket_recv_compare(SERVER_SOCKET, "HTTP/1.1\r\n"))
            {
                uart_writestr("HTTP version not 1.1\r\n");
                parse_state = ERR;
                break;
            }
        }
        else
        {
            //Check proper HTTP version
            if(!socket_recv_compare(SERVER_SOCKET, " HTTP/1.1\r\n"))
            {
                uart_writestr("HTTP version not 1.1\r\n");
                parse_state = ERR;
                break;
            }
        }

        if(!socket_recv_compare(SERVER_SOCKET, "Host: "))
        {
            parse_state = ERR;
            break;
        }

        int num=0;

        //parse ip address and store in local ip

        for(int i=0;i<4;i++)
        {
            if(!socket_recv_int(SERVER_SOCKET, &num))
            {
                parse_state = ERR;
                break;
            }

            message_ip[i] = num;

            if(!socket_recv_compare(SERVER_SOCKET, "."))
            {
                parse_state = ERR;
                break;
            }

            if(message_ip[i] != local_ip[i])
            {
                uart_writestr("IP address not matching \r\n");
                parse_state = ERR;
                break;
            }
        }

        socket_flush_line(SERVER_SOCKET);

        //match ip addresses


        /*
        if(!socket_recv_compare(SERVER_SOCKET, "Host: 192..100:8080\r\n") && !socket_recv_compare(SERVER_SOCKET, "Host: 127.0.0.100:8080\r\n"))
        {
            //IP address and port are not proper
            uart_writestr("IP address not matching\r\n");

        }
        */

        //socket_flush_line(SERVER_SOCKET);

        parse_state = HEADER;

        break;

    case HEADER:
        if(socket_is_blank_line(SERVER_SOCKET))
        {
            //Next line is blank line -- header section is ended

            //Flush the blank line
            socket_flush_line(SERVER_SOCKET);

            //Move to body section
            //uart_writestr("Moving to process section...\r\n");
            parse_state = PROCESS;
            break;
        }
        else
        {
            //Flush the header line because we have no use of it
            socket_flush_line(SERVER_SOCKET);
            //Stay in header state
            parse_state = HEADER;
        }
        break;

    case BODY:
        if(socket_is_blank_line(SERVER_SOCKET))
        {
            //Body has ended, move to processing ignoring the body

            //Flush the blank line
            socket_flush_line(SERVER_SOCKET);

            //Move to process
            parse_state = PROCESS;
            //uart_writestr("Moving to process section...\r\n");
            break;
        }
        else
        {
            //Flush body line because we don't need it
            socket_flush_line(SERVER_SOCKET);
            //Stay in body
            parse_state = BODY;
            break;
        }

        break;

    case ERR:

        //uart_writestr("In error state\r\n");

        //Send out error http
        socket_writestr(SERVER_SOCKET, "HTTP/1.1 400 INVALID\r\n");
        socket_writestr(SERVER_SOCKET, "Connection: close\r\n");
        socket_writestr(SERVER_SOCKET, "\r\n");

        //Close socket
        socket_disconnect(SERVER_SOCKET);

        //uart_writestr("In request line section...\r\n");
        parse_state = REQ_LINE;
        break;

    case PROCESS:

        //Need to process and send apt response

        //uart_writestr("Started executing process...\r\n");

        //Check request mode
        if(reqMode==0)
        {
            //Write out the device info as a JSON format

            //uart_writestr("Started sending response...\r\n");

            socket_writestr(SERVER_SOCKET, "HTTP/1.1 200 OK\r\n");
            socket_writestr(SERVER_SOCKET, "Content-Type: application/vnd.api+json\r\n");
            socket_writestr(SERVER_SOCKET, "Connection: close\r\n");
            socket_writestr(SERVER_SOCKET, "\r\n");

            //write JSON payload
            socket_writechar(SERVER_SOCKET, '{');

            socket_writequotedstring(SERVER_SOCKET, "vpd");
            socket_writechar(SERVER_SOCKET, ':');
            socket_writechar(SERVER_SOCKET, '{');

            socket_writequotedstring(SERVER_SOCKET, "model");
            socket_writechar(SERVER_SOCKET, ':');
            socket_writequotedstring(SERVER_SOCKET, vpd.model);
            socket_writechar(SERVER_SOCKET, ',');

            socket_writequotedstring(SERVER_SOCKET, "manufacturer");
            socket_writechar(SERVER_SOCKET, ':');
            socket_writequotedstring(SERVER_SOCKET, vpd.manufacturer);
            socket_writechar(SERVER_SOCKET, ',');

            socket_writequotedstring(SERVER_SOCKET, "serial_number");
            socket_writechar(SERVER_SOCKET, ':');
            socket_writequotedstring(SERVER_SOCKET, vpd.serial_number);
            socket_writechar(SERVER_SOCKET, ',');

            socket_writequotedstring(SERVER_SOCKET, "manufacture_date");
            socket_writechar(SERVER_SOCKET, ':');
            socket_writedate(SERVER_SOCKET, vpd.manufacture_date);
            socket_writechar(SERVER_SOCKET, ',');

            socket_writequotedstring(SERVER_SOCKET, "mac_address");
            socket_writechar(SERVER_SOCKET, ':');
            socket_write_macaddress(SERVER_SOCKET, vpd.mac_address);
            socket_writechar(SERVER_SOCKET, ',');

            socket_writequotedstring(SERVER_SOCKET, "country_code");
            socket_writechar(SERVER_SOCKET, ':');
            socket_writequotedstring(SERVER_SOCKET, vpd.country_of_origin);

            socket_writechar(SERVER_SOCKET, '}');
            socket_writechar(SERVER_SOCKET, ',');

            socket_writequotedstring(SERVER_SOCKET, "tcrit_hi");
            socket_writechar(SERVER_SOCKET, ':');
            socket_writedec32(SERVER_SOCKET, config.hi_alarm);
            socket_writechar(SERVER_SOCKET, ',');

            socket_writequotedstring(SERVER_SOCKET, "twarn_hi");
            socket_writechar(SERVER_SOCKET, ':');
            socket_writedec32(SERVER_SOCKET, config.hi_warn);
            socket_writechar(SERVER_SOCKET, ',');

            socket_writequotedstring(SERVER_SOCKET, "tcrit_lo");
            socket_writechar(SERVER_SOCKET, ':');
            socket_writedec32(SERVER_SOCKET, config.lo_alarm);
            socket_writechar(SERVER_SOCKET, ',');

            socket_writequotedstring(SERVER_SOCKET, "twarn_lo");
            socket_writechar(SERVER_SOCKET, ':');
            socket_writedec32(SERVER_SOCKET, config.lo_warn);
            socket_writechar(SERVER_SOCKET, ',');

            socket_writequotedstring(SERVER_SOCKET, "temperature");
            socket_writechar(SERVER_SOCKET, ':');
            socket_writedec32(SERVER_SOCKET, current_temperature);
            socket_writechar(SERVER_SOCKET, ',');

            socket_writequotedstring(SERVER_SOCKET, "state");
            socket_writechar(SERVER_SOCKET, ':');

            if(current_temperature > config.hi_alarm)                                               socket_writequotedstring(SERVER_SOCKET, "CRIT_HI");
            else if(current_temperature < config.lo_alarm)                                          socket_writequotedstring(SERVER_SOCKET, "CRIT_LO");
            else if(current_temperature > config.hi_warn && current_temperature <= config.hi_alarm) socket_writequotedstring(SERVER_SOCKET, "WARN_HI");
            else if(current_temperature < config.lo_warn && current_temperature >= config.lo_alarm) socket_writequotedstring(SERVER_SOCKET, "WARN_LO");
            else                                                                                    socket_writequotedstring(SERVER_SOCKET, "NORMAL");
            socket_writechar(SERVER_SOCKET, ',');

            socket_writequotedstring(SERVER_SOCKET, "log");
            socket_writechar(SERVER_SOCKET, ':');

            socket_writechar(SERVER_SOCKET, '[');
            unsigned long n=log_get_num_entries();
            for(unsigned long i=0; i<n;i++)
            {
                unsigned long time;
                unsigned char eventnum;

                log_get_record(i, (unsigned long*)&time, (unsigned char*)&eventnum);

                //Convert to JSON
                socket_writechar(SERVER_SOCKET, '{');

                socket_writequotedstring(SERVER_SOCKET, "timestamp");
                socket_writechar(SERVER_SOCKET, ':');
                socket_writedate(SERVER_SOCKET, time);
                socket_writechar(SERVER_SOCKET, ',');

                socket_writequotedstring(SERVER_SOCKET, "event");
                socket_writechar(SERVER_SOCKET, ':');
                socket_writedec32(SERVER_SOCKET, eventnum);

                socket_writechar(SERVER_SOCKET, '}');

                if(i<(n-1)) socket_writechar(SERVER_SOCKET, ',');

            }
            socket_writechar(SERVER_SOCKET, ']');

            socket_writechar(SERVER_SOCKET, '}');

            socket_writestr(SERVER_SOCKET, "\r\n");

            //uart_writestr("Ended sending response...\r\n");

            //Close socket
            socket_disconnect(SERVER_SOCKET);
        }


        else if (reqMode == 1 && resMode==1)
        {
            //Change the value

            /*
            *   Change mode -- indicates which variable to change
            *   tcrit_hi    0
            *   twarn_hi    1
            *   twarn_lo    2
            *   tcrit_lo    3
            *   none        -1
            *
            *   this would help us in the processing step
            */

            if(changeMode==0)
            {
                if(update_tcrit_hi(changeVal))
                {
                    parse_state = ERR;
                    break;
                }
            }
            else if(changeMode==1)
            {
                if(update_twarn_hi(changeVal))
                {
                    parse_state = ERR;
                    break;
                }
            }
            else if(changeMode==2)
            {
                if(update_twarn_lo(changeVal))
                {
                    parse_state = ERR;
                    break;
                }
            }
            else if(changeMode==3)
            {
                if(update_tcrit_lo(changeVal))
                {
                    parse_state = ERR;
                    break;
                }
            }

            config_set_modified();

            if(!eeprom_isbusy()) config_update();

            socket_writestr(SERVER_SOCKET, "HTTP/1.1 200 OK\r\n");
            socket_writestr(SERVER_SOCKET, "Connection: close\r\n");
            socket_writestr(SERVER_SOCKET, "\r\n");

            //Close socket
            socket_disconnect(SERVER_SOCKET);
        }

        else if(reqMode == 1 && resMode==0)
        {
            //reset

            socket_writestr(SERVER_SOCKET, "HTTP/1.1 200 OK\r\n");
            socket_writestr(SERVER_SOCKET, "Connection: close\r\n");
            socket_writestr(SERVER_SOCKET, "\r\n");

            if(resetMode)
            {
                //Close socket
                socket_disconnect(SERVER_SOCKET);
                wdt_force_restart();
            }

        }

        else if(reqMode==2 && resMode==2)
        {
            //DELETE log

            log_clear();
            if(!eeprom_isbusy()) log_update();

            socket_writestr(SERVER_SOCKET, "HTTP/1.1 200 OK\r\n");
            socket_writestr(SERVER_SOCKET, "Connection: close\r\n");
            socket_writestr(SERVER_SOCKET, "\r\n");

            //Close socket
            socket_disconnect(SERVER_SOCKET);
        }

        parse_state = REQ_LINE;

        break;

    default:
        break;
    }
}

