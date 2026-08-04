# NTP Packet Capture Steps

A pre-recorded NTP packet is already included in this directory as `ntp_stream.bin`. However, if you want to capture your own NTP packet, follow these steps:

1. Capture:

    ```bash
    sudo tcpdump -i enp2s1 -c 1 port 123 -s 0 -w ntp_packet.pcap
    ```

2. Extract UDP payload:

    ```bash
    tshark -r ntp_packet.pcap -T fields -e udp.payload > ntp_stream.hex
    ```

3. Convert hex to binary:

    ```bash
    xxd -r -p ntp_stream.hex > ntp_stream.bin
    ```
