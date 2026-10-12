#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p work/tests
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -Ifirmware/main -Ifirmware/components/usb_host_uvc/private_include \
    -Ifirmware/components/dns_server/include \
    tests/test_core.c firmware/main/form_codec.c firmware/main/frame_scale.c \
    firmware/components/dns_server/dns_packet.c \
    -o work/tests/test_core
./work/tests/test_core
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -pthread \
    -Itests/support -Ifirmware/main tests/test_video_store.c firmware/main/video_store.c \
    -o work/tests/test_video_store
./work/tests/test_video_store
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -Ifirmware/main tests/test_log_buffer.c firmware/main/log_buffer.c \
    -o work/tests/test_log_buffer
./work/tests/test_log_buffer
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -Itests/support/uvc_bulk -Itests/support -Ifirmware/components/usb_host_uvc/private_include \
    tests/test_uvc_bulk.c firmware/components/usb_host_uvc/uvc_bulk.c \
    -o work/tests/test_uvc_bulk
./work/tests/test_uvc_bulk
