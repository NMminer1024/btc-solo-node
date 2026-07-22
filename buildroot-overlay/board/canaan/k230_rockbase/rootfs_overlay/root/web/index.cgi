#!/bin/sh
printf 'Status: 302 Found\r\n'
printf 'Location: /index.html\r\n'
printf 'Cache-Control: no-store\r\n'
printf 'Content-Length: 0\r\n\r\n'
