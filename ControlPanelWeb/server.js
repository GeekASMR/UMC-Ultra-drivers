const express = require('express');
const { spawn } = require('child_process');
const http = require('http');
const WebSocket = require('ws');
const path = require('path');
const fs = require('fs');

const app = express();
const server = http.createServer(app);
const wss = new WebSocket.Server({ server });

app.use(express.static(path.join(__dirname, 'public')));
app.use(express.json());

const proxyPath = path.resolve(__dirname, '../build/bin/Release/UMCControlProxy.exe');
let proxy = spawn(proxyPath);

// The state cached locally
let currentState = { loopback: [], hwInGains: [], swPlayGains: [], hwInMutes: [], swPlayMutes: [] };

const STATE_FILE = 'totalmix_state.json';

// Restore state to proxy after it boots
setTimeout(() => {
    if (fs.existsSync(STATE_FILE)) {
        try {
            const savedState = JSON.parse(fs.readFileSync(STATE_FILE));
            // Send to proxy
            if (savedState.loopback) {
                savedState.loopback.forEach((lb, i) => proxy.stdin.write(`LOOPBACK ${i} ${lb ? 1 : 0}\n`));
            }
            if (savedState.hwInGains) {
                savedState.hwInGains.forEach((dests, src) => dests.forEach((val, dst) => {
                    proxy.stdin.write(`GAIN ${src} ${dst} ${val}\n`);
                }));
            }
            if (savedState.swPlayGains) {
                savedState.swPlayGains.forEach((dests, src) => dests.forEach((val, dst) => {
                    proxy.stdin.write(`GAIN ${src + 18} ${dst} ${val}\n`);
                }));
            }
            if (savedState.hwInMutes) {
                savedState.hwInMutes.forEach((dests, src) => dests.forEach((val, dst) => {
                    proxy.stdin.write(`MUTE ${src} ${dst} ${val ? 1 : 0}\n`);
                }));
            }
            if (savedState.swPlayMutes) {
                savedState.swPlayMutes.forEach((dests, src) => dests.forEach((val, dst) => {
                    proxy.stdin.write(`MUTE ${src + 18} ${dst} ${val ? 1 : 0}\n`);
                }));
            }
            console.log("Restored state from disk.");
        } catch(e) { console.error("Could not load state", e); }
    }
}, 500);

proxy.stdout.on('data', (data) => {
    const lines = data.toString().split('\n');
    for (let line of lines) {
        if (line.startsWith('STATE|')) {
            const parts = line.split('|');
            if (parts.length >= 6) {
                // Parse loopback
                currentState.loopback = parts[1].split(',').filter(x => x !== '').map(x => x === '1');
                
                // Parse gains & mutes
                const hwGroups = parts[2].split(';').filter(x => x !== '');
                currentState.hwInGains = hwGroups.map(g => g.split(',').filter(x => x !== '').map(Number));

                const swGroups = parts[3].split(';').filter(x => x !== '');
                currentState.swPlayGains = swGroups.map(g => g.split(',').filter(x => x !== '').map(Number));

                const hwMuteGroups = parts[4].split(';').filter(x => x !== '');
                currentState.hwInMutes = hwMuteGroups.map(g => g.split(',').filter(x => x !== '').map(x => x === '1'));

                const swMuteGroups = parts[5].split(';').filter(x => x !== '');
                currentState.swPlayMutes = swMuteGroups.map(g => g.split(',').filter(x => x !== '').map(x => x === '1'));

                // Parse Peaks (Apply log-like curve for better visualization)
                const toMeterHeight = (v) => { 
                    let p = parseFloat(v); 
                    if (isNaN(p)) return 0; 
                    if (p<0) p=0; if(p>1) p=1; 
                    return Math.pow(p, 0.45) * 100; 
                };
                currentState.hwInPeaks = (parts[6] ? parts[6].split(',').filter(x => x !== '').map(toMeterHeight) : Array(18).fill(0));
                currentState.swPlayPeaks = (parts[7] ? parts[7].split(',').filter(x => x !== '').map(toMeterHeight) : Array(20).fill(0));
                currentState.hwOutPeaks = (parts[8] ? parts[8].split(',').filter(x => x !== '').map(toMeterHeight) : Array(20).fill(0));

                // Broadcast state to all connected WS clients
                const msg = JSON.stringify({ type: 'state', state: currentState });
                wss.clients.forEach(client => {
                    if (client.readyState === 1) client.send(msg);
                });
            }
        }
    }
});

proxy.stderr.on('data', (data) => console.error(`PROXY ERR: ${data}`));

// Send GET command to proxy 10 times a second to poll state
setInterval(() => {
    if (proxy && !proxy.killed) {
        proxy.stdin.write('GET\n');
    }
}, 100);

// Auto-save state to disk every 3 seconds
setInterval(() => {
    try { fs.writeFileSync(STATE_FILE, JSON.stringify(currentState)); } catch(e) {}
}, 3000);

wss.on('connection', (ws) => {
    ws.send(JSON.stringify({ type: 'state', state: currentState }));
    ws.on('message', (msg) => {
        try {
            const data = JSON.parse(msg);
            if (data.type === 'gain') {
                if (data.srcType === 'hwIn') proxy.stdin.write(`GAIN ${data.srcIdx} ${data.dstIdx} ${data.value}\n`);
                else if (data.srcType === 'swPlay') proxy.stdin.write(`GAIN ${data.srcIdx + 18} ${data.dstIdx} ${data.value}\n`);
            } else if (data.type === 'mute') {
                if (data.srcType === 'hwIn') proxy.stdin.write(`MUTE ${data.srcIdx} ${data.dstIdx} ${data.value ? 1 : 0}\n`);
                else if (data.srcType === 'swPlay') proxy.stdin.write(`MUTE ${data.srcIdx + 18} ${data.dstIdx} ${data.value ? 1 : 0}\n`);
            } else if (data.type === 'loopback') {
                proxy.stdin.write(`LOOPBACK ${data.dstIdx} ${data.value ? 1 : 0}\n`);
            }
        } catch(e) { console.error('WS msg error', e); }
    });
});

const PORT = 8080;
server.listen(PORT, () => {
    console.log(`🚀 TotalMix Control Panel running on http://localhost:${PORT}`);
});
