const ws = new WebSocket(`ws://${location.host}`);

let globalState = null;
let currentSubmix = parseInt(localStorage.getItem('currentSubmix')) || 0;

function loadLocalArray(key, fallback) {
    let stored = localStorage.getItem(key);
    return stored ? JSON.parse(stored) : fallback;
}
let linksHwIn = loadLocalArray('linksHwIn', [false, false, false, false, true, true, true, true, true]);
let linksSwPlay = loadLocalArray('linksSwPlay', [false, false, false, false, false, true, true, true, true, true]);
let linksHwOut = loadLocalArray('linksHwOut', [false, false, false, false, false, true, true, true, true, true]);

function saveUIState() {
    localStorage.setItem('currentSubmix', currentSubmix);
    localStorage.setItem('linksHwIn', JSON.stringify(linksHwIn));
    localStorage.setItem('linksSwPlay', JSON.stringify(linksSwPlay));
    localStorage.setItem('linksHwOut', JSON.stringify(linksHwOut));
}

// Arrays mapping internal names to UI labels
const hwInNames = [
    "ANA IN 1","ANA IN 2","ANA IN 3","ANA IN 4","ANA IN 5","ANA IN 6","ANA IN 7","ANA IN 8",
    "SPD IN L","SPD IN R",
    "ADT IN 1","ADT IN 2","ADT IN 3","ADT IN 4","ADT IN 5","ADT IN 6","ADT IN 7","ADT IN 8"
];
const hwOutNames = [
    "ANA OUT 1","ANA OUT 2","ANA OUT 3","ANA OUT 4","ANA OUT 5","ANA OUT 6","ANA OUT 7","ANA OUT 8","ANA OUT 9","ANA OUT 10",
    "SPD OUT L","SPD OUT R",
    "ADT OUT 1","ADT OUT 2","ADT OUT 3","ADT OUT 4","ADT OUT 5","ADT OUT 6","ADT OUT 7","ADT OUT 8"
];
const swNames = hwOutNames.map(n => n.replace('ANA OUT', 'PLAY').replace('SPD OUT', 'PLY SPD').replace('ADT OUT', 'PLY ADT'));

const hwInContainer = document.getElementById('hwin-strips');
const swPlayContainer = document.getElementById('swplay-strips');
const hwOutContainer = document.getElementById('hwout-strips');

let needsFullRender = true;

ws.onmessage = (event) => {
    const data = JSON.parse(event.data);
    if (data.type === 'state') {
        globalState = data.state;
        if (needsFullRender) {
            renderAll();
            needsFullRender = false;
        } else {
            updateValues();
        }
    }
};

function formatDb(linear) {
    if (linear <= 0.0001) return "-∞";
    let db = 20 * Math.log10(linear);
    if (db > 0) return `+${db.toFixed(1)}`;
    return `${db.toFixed(1)}`;
}

function sendGain(srcType, srcIdx, dstIdx, value) { ws.send(JSON.stringify({ type: 'gain', srcType, srcIdx, dstIdx, value })); }

function sendMute(srcType, srcIdx, dstIdx, value) { ws.send(JSON.stringify({ type: 'mute', srcType, srcIdx, dstIdx, value })); }

function sendLoopback(dstIdx, value) { ws.send(JSON.stringify({ type: 'loopback', dstIdx, value })); }
window.toggleLinkHwIn = function(pairIdx) { linksHwIn[pairIdx] = !linksHwIn[pairIdx]; saveUIState(); needsFullRender = true; renderAll(); };
window.toggleLinkSwPlay = function(pairIdx) { linksSwPlay[pairIdx] = !linksSwPlay[pairIdx]; saveUIState(); needsFullRender = true; renderAll(); };
window.toggleLinkHwOut = function(pairIdx) { linksHwOut[pairIdx] = !linksHwOut[pairIdx]; saveUIState(); needsFullRender = true; renderAll(); };

window.toggleMute = function(type, idx, isLinked) {
    if(!globalState) return;
    const isMuted = type === 'hwIn' ? globalState.hwInMutes[idx][currentSubmix] : globalState.swPlayMutes[idx][currentSubmix];
    sendMute(type, idx, currentSubmix, !isMuted);
    if (isLinked) {
        sendMute(type, idx+1, (currentSubmix % 2 === 0 ? currentSubmix+1 : currentSubmix), !isMuted);
    }
};

function calculatePanAndV(gainL, gainR) {
    if (gainL === 0 && gainR === 0) return { v: 0, pan: 0 };
    let v = Math.max(gainL, gainR);
    let pan = 0;
    if (gainL >= gainR) { pan = (gainR / v) - 1; } else { pan = 1 - (gainL / v); }
    return { v, pan };
}

function getGainsFromPanAndV(v, pan) {
    let gainL = v * Math.min(1, 1 - pan);
    let gainR = v * Math.min(1, 1 + pan);
    return { gainL, gainR };
}

// --- Rotary Pan Knob Drag Logic ---
let isDraggingPan = false;
let currentPanIdx = null;
let currentPanType = null;
let startY = 0;
let startPan = 0;

window.startPanDrag = function(e, idx, type, isLinked) {
    isDraggingPan = true; currentPanIdx = idx; currentPanType = type; startY = e.clientY;
    window.currentPanLinked = isLinked;
    let submixL = Math.floor(currentSubmix/2) * 2;
    let gains = type === 'hwIn' ? globalState.hwInGains : globalState.swPlayGains;
    let gainL = gains[idx][submixL] || 0;
    let gainR = isLinked ? (gains[idx+1] ? gains[idx+1][submixL+1] : 0) : (gains[idx][submixL+1] || 0);
    
    let pv = calculatePanAndV(gainL, gainR);
    startPan = pv.pan;
    document.addEventListener('mousemove', onPanDrag); document.addEventListener('mouseup', endPanDrag);
    e.preventDefault();
};

function onPanDrag(e) {
    if (!isDraggingPan) return;
    let dy = startY - e.clientY; 
    let newPan = startPan + (dy * 0.015);
    if (newPan > 1) newPan = 1; if (newPan < -1) newPan = -1;
    
    let faderEl = document.getElementById(`fader-${currentPanType}-${currentPanIdx}`);
    let v = faderEl ? parseFloat(faderEl.value) : 0;
    let submixL = Math.floor(currentSubmix/2) * 2;
    let gains = getGainsFromPanAndV(v, newPan);
    
    if (window.currentPanLinked) {
        sendGain(currentPanType, currentPanIdx, submixL, gains.gainL);
        sendGain(currentPanType, currentPanIdx+1, submixL+1, gains.gainR);
    } else {
        sendGain(currentPanType, currentPanIdx, submixL, gains.gainL);
        sendGain(currentPanType, currentPanIdx, submixL+1, gains.gainR);
    }
    
    // Visually update immediately
    let knob = document.getElementById(`pan-${currentPanType}-${currentPanIdx}`);
    if (knob) knob.style.transform = `rotate(${newPan * 135}deg)`;
    let label = document.getElementById(`pan-label-${currentPanType}-${currentPanIdx}`);
    if (label) {
        if (Math.abs(newPan) < 0.01) label.innerText = "C"; 
        else if (newPan < 0) label.innerText = "L" + Math.round(-newPan*100); 
        else label.innerText = "R" + Math.round(newPan*100);
    }
}

function endPanDrag(e) {
    isDraggingPan = false;
    document.removeEventListener('mousemove', onPanDrag); document.removeEventListener('mouseup', endPanDrag);
}

window.resetPan = function(idx, type, isLinked) {
    let faderEl = document.getElementById(`fader-${type}-${idx}`);
    let v = faderEl ? parseFloat(faderEl.value) : 0;
    let submixL = Math.floor(currentSubmix/2) * 2;
    let gains = getGainsFromPanAndV(v, 0); // 0 = Center
    if (isLinked) {
        sendGain(type, idx, submixL, gains.gainL);
        sendGain(type, idx+1, submixL+1, gains.gainR);
    } else {
        sendGain(type, idx, submixL, gains.gainL);
        sendGain(type, idx, submixL+1, gains.gainR);
    }
};

window.updateFader = function(el, idx, type, isLinked) {
    const v = parseFloat(el.value);
    el.parentElement.nextElementSibling.innerText = formatDb(v);
    let submixL = Math.floor(currentSubmix/2) * 2;
    
    let gainsSource = type === 'hwIn' ? globalState.hwInGains : globalState.swPlayGains;
    let gainL = gainsSource[idx] ? gainsSource[idx][submixL] : 0;
    let gainR = isLinked ? (gainsSource[idx+1] ? gainsSource[idx+1][submixL+1] : 0) : (gainsSource[idx] ? gainsSource[idx][submixL+1] : 0);
    let pv = calculatePanAndV(gainL, gainR);
    let newGains = getGainsFromPanAndV(v, pv.pan);

    if (isLinked) {
        sendGain(type, idx, submixL, newGains.gainL);
        sendGain(type, idx+1, submixL+1, newGains.gainR);
    } else {
        sendGain(type, idx, submixL, newGains.gainL);
        sendGain(type, idx, submixL+1, newGains.gainR);
    }
};

window.selectSubmix = function(idx) {
    currentSubmix = idx; 
    saveUIState();
    needsFullRender = true;
    renderAll();
};
window.toggleLoopback = function(idx, isLinked) {
    sendLoopback(idx, !globalState.loopback[idx]);
    if (isLinked) sendLoopback(idx+1, !globalState.loopback[idx]);
};

function getMergedName(i, baseNames, prefixA, prefixS, prefixD) {
    if (i === 10 && prefixS) return `${prefixS} L/R`;
    if (i === 8 && !prefixS) return `SPD IN L/R`;
    if (i < 10 && prefixA) return `${prefixA} ${i+1}/${i+2}`;
    if (i < 8 && !prefixA) return `ANA IN ${i+1}/${i+2}`;
    
    if (i >= (prefixA ? 12 : 10)) {
        const adatNum = i - (prefixA ? 12 : 10) + 1;
        return `${prefixD} ${adatNum}/${adatNum+1}`;
    }
    return baseNames[i];
}

function updateValues() {
    if (!globalState) return;
    
    let submixL = Math.floor(currentSubmix/2) * 2;
    let submixR = submixL + 1;

    // HW Inputs
    for(let i=0; i<18; i++) {
        let isLinked = (i % 2 === 0) ? linksHwIn[i/2] : false;
        if (i % 2 === 1 && linksHwIn[Math.floor(i/2)]) continue;
        
        let subR = submixL + 1;
        let gainL = globalState.hwInGains[i] ? (globalState.hwInGains[i][submixL] || 0) : 0;
        let gainR = isLinked ? 
            (globalState.hwInGains[i+1] ? (globalState.hwInGains[i+1][subR] || 0) : 0) : 
            (globalState.hwInGains[i] ? (globalState.hwInGains[i][subR] || 0) : 0);
        const mute = globalState.hwInMutes[i] ? (globalState.hwInMutes[i][currentSubmix] || false) : false;
        
        let pv = calculatePanAndV(gainL, gainR);
        let v = pv.v; let pan = pv.pan;
        
        const fader = document.getElementById(`fader-hwIn-${i}`);
        const panKnob = document.getElementById(`pan-hwIn-${i}`);
        
        if (fader && document.activeElement !== fader) {
            fader.value = v; fader.parentElement.nextElementSibling.innerText = formatDb(v);
        }
        if (panKnob && (!isDraggingPan || currentPanIdx !== i || currentPanType !== 'hwIn')) {
            panKnob.style.transform = `rotate(${pan * 135}deg)`;
            let pLabel = document.getElementById(`pan-label-hwIn-${i}`);
            if (pLabel) {
                if (Math.abs(pan) < 0.01) pLabel.innerText = "C"; else if (pan < 0) pLabel.innerText = "L" + Math.round(-pan*100); else pLabel.innerText = "R" + Math.round(pan*100);
            }
        }
        
        let pL = globalState.hwInPeaks ? (globalState.hwInPeaks[i] || 0) : 0;
        let pR = isLinked ? (globalState.hwInPeaks ? (globalState.hwInPeaks[i+1] || 0) : 0) : 0;
        
        // Handle Meter GUI
        let mL = document.getElementById(`meter-L-hwIn-${i}`);
        if (mL) mL.style.height = `${pL}%`;
        if (isLinked) {
            let mR = document.getElementById(`meter-R-hwIn-${i}`);
            if (mR) mR.style.height = `${pR}%`;
        }
    }

    // SW Playbacks
    for(let i=0; i<20; i++) {
        let isLinked = (i % 2 === 0) ? linksSwPlay[i/2] : false;
        if (i % 2 === 1 && linksSwPlay[Math.floor(i/2)]) continue;
        
        let subR = submixL + 1;
        let gainL = globalState.swPlayGains[i] ? (globalState.swPlayGains[i][submixL] || 0) : 0;
        let gainR = isLinked ? 
            (globalState.swPlayGains[i+1] ? (globalState.swPlayGains[i+1][subR] || 0) : 0) : 
            (globalState.swPlayGains[i] ? (globalState.swPlayGains[i][subR] || 0) : 0);
        let mute = globalState.swPlayMutes[i] ? (globalState.swPlayMutes[i][currentSubmix] || false) : false;
        
        let pv = calculatePanAndV(gainL, gainR);
        let v = pv.v; let pan = pv.pan;
        
        const fader = document.getElementById(`fader-swPlay-${i}`);
        const panKnob = document.getElementById(`pan-swPlay-${i}`);
        
        if (fader && document.activeElement !== fader) {
            fader.value = v; fader.parentElement.nextElementSibling.innerText = formatDb(v);
        }
        if (panKnob && (!isDraggingPan || currentPanIdx !== i || currentPanType !== 'swPlay')) {
            panKnob.style.transform = `rotate(${pan * 135}deg)`;
            let pLabel = document.getElementById(`pan-label-swPlay-${i}`);
            if (pLabel) {
                if (Math.abs(pan) < 0.01) pLabel.innerText = "C"; else if (pan < 0) pLabel.innerText = "L" + Math.round(-pan*100); else pLabel.innerText = "R" + Math.round(pan*100);
            }
        }

        let pL = globalState.swPlayPeaks ? (globalState.swPlayPeaks[i] || 0) : 0;
        let pR = isLinked ? (globalState.swPlayPeaks ? (globalState.swPlayPeaks[i+1] || 0) : 0) : 0;
        
        let mL = document.getElementById(`meter-L-swPlay-${i}`);
        if (mL) mL.style.height = `${pL}%`;
        if (isLinked) {
            let mR = document.getElementById(`meter-R-swPlay-${i}`);
            if (mR) mR.style.height = `${pR}%`;
        }
    }

    // HW Outputs (Loopback)
    for(let i=0; i<20; i++) {
        let isLinked = (i % 2 === 0) ? linksHwOut[i/2] : false;
        if (i % 2 === 1 && linksHwOut[Math.floor(i/2)]) continue;
        const lbBtn = document.getElementById(`lb-hwOut-${i}`);
        if (lbBtn) {
            if (globalState.loopback[i]) lbBtn.classList.add('active'); else lbBtn.classList.remove('active');
        }
    }
}

function renderAll() {
    if (!globalState || !globalState.loopback || globalState.loopback.length === 0) return;

    // --- 1. HARDWARE OUTPUTS ---
    hwOutContainer.innerHTML = '';
    for(let i=0; i<20; i++) {
        let isLinked = (i % 2 === 0) ? linksHwOut[i/2] : false;
        if (i % 2 === 1 && linksHwOut[Math.floor(i/2)]) continue; 

        const strip = document.createElement('div');
        let selected = (currentSubmix === i || (isLinked && currentSubmix === i+1));
        strip.className = `channel-strip ${selected ? 'active-submix' : ''}` + (isLinked ? ' stereo-strip' : '');
        
        if (i === 9 || i === 11 || (isLinked && (i === 8 || i === 10))) strip.style.marginRight = '16px';
        
        let name = isLinked ? getMergedName(i, hwOutNames, "ANA OUT", "SPD OUT", "ADT OUT") : hwOutNames[i];
        let linkHTML = (i < 10) ? `<button class="btn-small btn-link ${(i%2===0 && linksHwOut[i/2]) ? 'active' : ''}" 
             onclick="toggleLinkHwOut(${Math.floor(i/2)})" style="margin-top:4px;">${(i%2===0 && linksHwOut[i/2]) ? '⛓️  ST' : '🔗  M'}</button>` : '';

        strip.innerHTML = `
            <div class="channel-name">${name}</div>
            <button id="lb-hwOut-${i}" class="btn-small btn-loopback ${globalState.loopback[i] ? 'active' : ''}" onclick="toggleLoopback(${i}, ${isLinked})">LB</button>
            <div style="flex:1"></div>
            <button class="btn-small btn-select ${selected ? 'active' : ''}" onclick="selectSubmix(${i})">SEL</button>
            ${linkHTML}
        `;
        hwOutContainer.appendChild(strip);
    }

    let submixL = Math.floor(currentSubmix/2) * 2;
    let submixR = submixL + 1;

    // --- 2. HARDWARE INPUTS ---
    hwInContainer.innerHTML = '';
    for(let i=0; i<18; i++) {
        let isLinked = (i % 2 === 0) ? linksHwIn[i/2] : false;
        if (i % 2 === 1 && linksHwIn[Math.floor(i/2)]) continue; 
        
        let gainL = globalState.hwInGains[i] ? (globalState.hwInGains[i][submixL] || 0) : 0;
        let gainR = isLinked ? 
            (globalState.hwInGains[i+1] ? (globalState.hwInGains[i+1][submixR] || 0) : 0) : 
            (globalState.hwInGains[i] ? (globalState.hwInGains[i][submixR] || 0) : 0);
        let mute = globalState.hwInMutes[i] ? (globalState.hwInMutes[i][currentSubmix] || false) : false;
        
        let pv = calculatePanAndV(gainL, gainR);
        let v = pv.v; let pan = pv.pan;

        let name = isLinked ? getMergedName(i, hwInNames, false, false, "ADT IN") : hwInNames[i];

        const strip = document.createElement('div');
        strip.className = 'channel-strip' + (isLinked ? ' stereo-strip' : '');
        if (i === 7 || i === 9 || (isLinked && (i === 6 || i === 8))) strip.style.marginRight = '16px';

        let linkBtnHTML = (i < 8) ? `
            <button class="btn-small btn-link ${(i%2===0 && linksHwIn[Math.floor(i/2)]) ? 'active' : ''}" 
             onclick="toggleLinkHwIn(${Math.floor(i/2)})" style="margin-top:4px;">${(i%2===0 && linksHwIn[Math.floor(i/2)]) ? '⛓️  ST' : '🔗  M'}</button>
        ` : '';

        let panHTML = `
            <div class="pan-wrapper">
                <div id="pan-label-hwIn-${i}" class="pan-label">${pan === 0 ? 'C' : (pan < 0 ? 'L'+Math.round(-pan*100) : 'R'+Math.round(pan*100))}</div>
                <div class="pan-knob-container" onmousedown="startPanDrag(event, ${i}, 'hwIn', ${isLinked})" ondblclick="resetPan(${i}, 'hwIn', ${isLinked})">
                    <div id="pan-hwIn-${i}" class="pan-knob" style="transform: rotate(${pan * 135}deg)">
                        <div class="pan-knob-indicator"></div>
                    </div>
                </div>
            </div>
        `;

        strip.innerHTML = `
            <div class="channel-name">${name}</div>
            ${panHTML}
            <div class="fader-meter-group">
                <div class="meters-container">
                    <div class="meter-track"><div class="meter-fill" id="meter-L-hwIn-${i}"></div></div>
                    ${isLinked ? `<div class="meter-track"><div class="meter-fill" id="meter-R-hwIn-${i}"></div></div>` : ''}
                </div>
                <div class="fader-wrapper">
                    <input id="fader-hwIn-${i}" type="range" class="fader-input" orient="vertical" min="0" max="1" step="0.005" value="${v}" 
                        oninput="updateFader(this, ${i}, 'hwIn', ${isLinked})">
                </div>
            </div>
            <div class="fader-value">${formatDb(v)}</div>
            ${linkBtnHTML}
        `;
        hwInContainer.appendChild(strip);
    }

    // --- 3. SOFTWARE PLAYBACKS ---
    swPlayContainer.innerHTML = '';
    for(let i=0; i<20; i++) {
        let isLinked = (i % 2 === 0) ? linksSwPlay[i/2] : false;
        if (i % 2 === 1 && linksSwPlay[Math.floor(i/2)]) continue; 

        let gainL = globalState.swPlayGains[i] ? (globalState.swPlayGains[i][submixL] || 0) : 0;
        let gainR = isLinked ? 
            (globalState.swPlayGains[i+1] ? (globalState.swPlayGains[i+1][submixR] || 0) : 0) : 
            (globalState.swPlayGains[i] ? (globalState.swPlayGains[i][submixR] || 0) : 0);
        let mute = globalState.swPlayMutes[i] ? (globalState.swPlayMutes[i][currentSubmix] || false) : false;
        
        let pv = calculatePanAndV(gainL, gainR);
        let v = pv.v; let pan = pv.pan;
        
        let name = isLinked ? getMergedName(i, swNames, "PLAY", "PLY SPD", "PLY ADT") : swNames[i];

        const strip = document.createElement('div');
        strip.className = 'channel-strip' + (isLinked ? ' stereo-strip' : '');
        if (i === 9 || i === 11 || (isLinked && (i === 8 || i === 10))) strip.style.marginRight = '16px';

        let linkBtnHTML = (i < 10) ? `
            <button class="btn-small btn-link ${(i%2===0 && linksSwPlay[Math.floor(i/2)]) ? 'active' : ''}" 
             onclick="toggleLinkSwPlay(${Math.floor(i/2)})" style="margin-top:4px;">${(i%2===0 && linksSwPlay[Math.floor(i/2)]) ? '⛓️  ST' : '🔗  M'}</button>
        ` : '';

        let panHTML = `
            <div class="pan-wrapper">
                <div id="pan-label-swPlay-${i}" class="pan-label">${pan === 0 ? 'C' : (pan < 0 ? 'L'+Math.round(-pan*100) : 'R'+Math.round(pan*100))}</div>
                <div class="pan-knob-container" onmousedown="startPanDrag(event, ${i}, 'swPlay', ${isLinked})" ondblclick="resetPan(${i}, 'swPlay', ${isLinked})">
                    <div id="pan-swPlay-${i}" class="pan-knob" style="transform: rotate(${pan * 135}deg)">
                        <div class="pan-knob-indicator"></div>
                    </div>
                </div>
            </div>
        `;

        strip.innerHTML = `
            <div class="channel-name">${name}</div>
            ${panHTML}
            <div class="fader-meter-group">
                <div class="meters-container">
                    <div class="meter-track"><div class="meter-fill" id="meter-L-swPlay-${i}"></div></div>
                    ${isLinked ? `<div class="meter-track"><div class="meter-fill" id="meter-R-swPlay-${i}"></div></div>` : ''}
                </div>
                <div class="fader-wrapper">
                    <input id="fader-swPlay-${i}" type="range" class="fader-input" orient="vertical" min="0" max="1" step="0.005" value="${v}" 
                        oninput="updateFader(this, ${i}, 'swPlay', ${isLinked})">
                </div>
            </div>
            <div class="fader-value">${formatDb(v)}</div>
            ${linkBtnHTML}
        `;
        swPlayContainer.appendChild(strip);
    }
}
