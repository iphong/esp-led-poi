#!/bin/bash

# Must use Node 16. Node 17 and greater will throw errors and build will fail

cd app
yarn install
npx webpack

# cp -R node_modules/@mdi/font/css/* ./css
# cp -R node_modules/@mdi/font/fonts/* ./fonts

zip app.zip dist/* css/* fonts/* img/* js/* app.html remote.html utils.html manifest.json
cd ..

mkdir -p build
rm -Rf build/*
mv app/app.zip build/

pio run -e rx_uart
pio run -e tx_uart

cp .pio/build/rx_uart/firmware.bin build/rx.bin
cp .pio/build/tx_uart/firmware.bin build/tx.bin

