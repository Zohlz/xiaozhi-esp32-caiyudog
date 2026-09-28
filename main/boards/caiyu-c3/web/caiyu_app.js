(() => {
    'use strict';

    const LABELS = {
        neutral: ['平静', '😐'],
        happy: ['开心', '😊'],
        laughing: ['大笑', '🤣'],
        funny: ['滑稽', '😂'],
        sad: ['难过', '😢'],
        angry: ['生气', '😠'],
        crying: ['哭泣', '😭'],
        loving: ['喜欢', '🥰'],
        embarrassed: ['尴尬', '😳'],
        surprised: ['惊讶', '😲'],
        shocked: ['震惊', '😱'],
        thinking: ['思考', '🤔'],
        winking: ['眨眼', '😉'],
        cool: ['酷', '😎'],
        relaxed: ['放松', '😌'],
        delicious: ['好吃', '😋'],
        kissy: ['亲亲', '😘'],
        confident: ['自信', '😏'],
        sleepy: ['困倦', '😴'],
        silly: ['搞怪', '😜'],
        confused: ['困惑', '😵'],
    };

    const LAMP_MODES = { 0: '关灯', 1: '常亮', 2: '呼吸', 3: '彩虹', 4: '流水', 5: '闪烁' };
    const ACTION_MODE = { off: 0, on: 1, breathe: 2, rainbow: 3, flow: 4, flash: 5 };
    const COLORED_ACTIONS = new Set(['on', 'flow', 'flash']);

    const ACTION_LABELS = {
        stand: ['站立', '🐕'],
        sit: ['坐下', '🪑'],
        sleep: ['睡觉', '😴'],
        prone: ['趴下', '🛌'],
        front_prone: ['前趴', '🙇'],
        raise_hips: ['抬屁股', '🍑'],
        wave: ['挥手', '👋'],
        stretch: ['伸懒腰', '🙆'],
        swing: ['左右摇摆', '💃'],
        sway: ['前后摇摆', '🕺'],
        scratch: ['挠痒', '🦵'],
        kneel: ['跪拜', '🧎'],
        shake_back_legs: ['抖后腿', '🐾'],
    };
    const JOYSTICK_ACTIONS = new Set(['walk', 'walk_back', 'turn_left', 'turn_right']);
    const ACTION_HIGHLIGHT_MS = 20000;

    const CHAT_STATES = {
        idle: ['唤醒对话', '🎤', false],
        unknown: ['唤醒对话', '🎤', false],
        connecting: ['连接中…', '⏳', true],
        listening: ['聆听中…', '👂', true],
        speaking: ['说话中…', '🔊', true],
        activating: ['激活中…', '⏳', true],
        upgrading: ['升级中…', '⬆️', true],
        wifi_configuring: ['配网中…', '📶', true],
        audio_testing: ['音频测试中…', '🎧', true],
        starting: ['启动中…', '⏳', true],
        fatal_error: ['设备异常', '⚠️', true],
        pending: ['处理中…', '⏳', true],
    };
    const CHAT_REQUERY_MS = 250;

    const LEG_LABELS = ['左前', '右前', '左后', '右后'];
    const TRIM_DEFAULT_MAX = 20;

    const DIR_LABEL = {
        forward: '前进',
        backward: '后退',
        left: '左转',
        right: '右转',
    };
    const JOY_DEAD_ZONE = 0.28;

    const PING_INTERVAL = 5000;
    const RECONNECT_MIN = 1500;
    const RECONNECT_MAX = 8000;
    const MAX_LOG_LINES = 60;
    const SLIDER_THROTTLE = 180;

    const el = {
        dot: document.getElementById('dot'),
        wsLabel: document.getElementById('ws-label'),
        latency: document.getElementById('latency'),
        board: document.getElementById('board-name'),
        tabbar: document.getElementById('tabbar'),
        auto: document.getElementById('btn-auto'),
        hint: document.getElementById('mode-hint'),
        grid: document.getElementById('grid'),
        actionGrid: document.getElementById('action-grid'),
        actionStatus: document.getElementById('action-status'),
        joyBg: document.getElementById('joy-bg'),
        joyThumb: document.getElementById('joy-thumb'),
        joyRing: document.getElementById('joy-ring'),
        joyDir: document.getElementById('joy-dir'),
        stand: document.getElementById('btn-stand'),
        effects: document.getElementById('effects'),
        color: document.getElementById('led-color'),
        colorHex: document.getElementById('color-hex'),
        r: document.getElementById('led-r'),
        g: document.getElementById('led-g'),
        b: document.getElementById('led-b'),
        rVal: document.getElementById('r-val'),
        gVal: document.getElementById('g-val'),
        bVal: document.getElementById('b-val'),
        brightness: document.getElementById('led-brightness'),
        brightnessVal: document.getElementById('brightness-val'),
        lampStatus: document.getElementById('lamp-status'),
        lampSpeed: document.getElementById('lamp-speed'),
        lampSpeedVal: document.getElementById('lamp-speed-val'),
        speed: document.getElementById('action-speed'),
        speedVal: document.getElementById('speed-val'),
        log: document.getElementById('log'),
        clear: document.getElementById('btn-clear'),
        chat: document.getElementById('btn-chat'),
        chatIco: document.getElementById('chat-ico'),
        chatLabel: document.getElementById('chat-label'),
        chatStatus: document.getElementById('chat-status'),
        trims: [0, 1, 2, 3].map((i) => document.getElementById(`trim-${i}`)),
        trimInps: [0, 1, 2, 3].map((i) => document.getElementById(`trim-inp-${i}`)),
        trimSave: document.getElementById('btn-trim-save'),
        trimReset: document.getElementById('btn-trim-reset'),
        trimStatus: document.getElementById('trim-status'),
    };

    let ws = null;
    let pingTimer = null;
    let reconnectTimer = null;
    let reconnectDelay = RECONNECT_MIN;
    let activeButton = null;
    let colorTimer = null;
    let brightnessTimer = null;
    let lampSpeedTimer = null;
    let speedTimer = null;
    let actionButton = null;
    let actionTimer = null;
    let chatState = null;
    let chatTimer = null;
    let trims = [0, 0, 0, 0];
    let trimMax = TRIM_DEFAULT_MAX;
    let trimTimer = null;
    let trimPending = 0;
    let joyDir = null;
    let joyDragging = false;
    let actionSpeed = 100;

    const lamp = { on: false, mode: 0, brightness: 80, speed: 100, r: 255, g: 107, b: 53 };

    function log(text, level) {
        const line = document.createElement('div');

        const time = document.createElement('span');
        time.className = 't';
        time.textContent = new Date().toLocaleTimeString('zh-CN', { hour12: false });

        const body = document.createElement('span');
        body.className = level || 'info';
        body.textContent = text;

        line.append(time, body);
        el.log.appendChild(line);
        while (el.log.childElementCount > MAX_LOG_LINES) {
            el.log.removeChild(el.log.firstChild);
        }
        el.log.scrollTop = el.log.scrollHeight;
    }

    function connect() {
        const url = `ws://${location.host}/ws`;
        log(`连接 ${url}`);

        try {
            ws = new WebSocket(url);
        } catch (err) {
            log(`创建 WebSocket 失败：${err}`, 'err');
            scheduleReconnect();
            return;
        }

        ws.onopen = () => {
            el.dot.classList.add('online');
            el.wsLabel.textContent = '已连接';
            reconnectDelay = RECONNECT_MIN;
            log('已连接', 'ok');
            send({ type: 'hello' });
            send({ type: 'lamp', action: 'query' });
            send({ type: 'action', action: 'query' });
            send({ type: 'speed' });
            send({ type: 'chat' });
            send({ type: 'trim' });
            startPing();
        };

        ws.onmessage = (event) => {
            let msg;
            try {
                msg = JSON.parse(event.data);
            } catch (err) {
                log(`收到无法解析的消息：${event.data}`, 'warn');
                return;
            }
            handleMessage(msg);
        };

        ws.onclose = () => {
            el.dot.classList.remove('online');
            el.wsLabel.textContent = '已断开';
            el.board.textContent = '--';
            stopPing();
            setChatState('unknown');
            log('连接已断开', 'warn');
            scheduleReconnect();
        };

        ws.onerror = () => {
            log('连接出错', 'err');
        };
    }

    function scheduleReconnect() {
        if (reconnectTimer) {
            return;
        }
        reconnectTimer = setTimeout(() => {
            reconnectTimer = null;
            connect();
        }, reconnectDelay);
        reconnectDelay = Math.min(reconnectDelay * 2, RECONNECT_MAX);
    }

    function send(obj) {
        if (!ws || ws.readyState !== WebSocket.OPEN) {
            log('未连接，指令已丢弃', 'warn');
            return false;
        }
        ws.send(JSON.stringify(obj));
        return true;
    }

    function startPing() {
        stopPing();
        pingTimer = setInterval(() => send({ type: 'ping', t: Date.now() }), PING_INTERVAL);
    }

    function stopPing() {
        if (pingTimer) {
            clearInterval(pingTimer);
            pingTimer = null;
        }
        el.latency.textContent = '-- ms';
    }

    function handleMessage(msg) {
        switch (msg.type) {
            case 'hello':
                el.board.textContent = `${msg.board || 'unknown'} ${msg.firmware || ''}`.trim();
                renderEmotions(msg.emotions || []);
                renderActions(msg.actions || []);
                log(`设备上报 ${msg.emotions ? msg.emotions.length : 0} 个表情 / ` +
                    `${msg.actions ? msg.actions.length : 0} 个动作`, 'ok');
                break;

            case 'pong':
                if (typeof msg.t === 'number') {
                    el.latency.textContent = `${Date.now() - msg.t} ms`;
                }
                break;

            case 'state':
                setActiveEmotion(msg.preview ? msg.emotion : null);
                break;

            case 'lamp':
                applyLampState(msg);
                break;

            case 'action':
                applyActionState(msg);
                break;

            case 'speed':
                applySpeedState(msg);
                break;

            case 'drive':
                break;

            case 'chat':
                setChatState(msg.state || 'unknown');
                break;

            case 'trim':
                applyTrimState(msg);
                break;

            default:
                log(`未知消息：${JSON.stringify(msg)}`, 'warn');
        }
    }

    function renderEmotions(names) {
        const fragment = document.createDocumentFragment();

        names.forEach((name) => {
            const [label, icon] = LABELS[name] || [name, '❓'];

            const btn = document.createElement('button');
            btn.className = 'emo';
            btn.dataset.emotion = name;

            const iconEl = document.createElement('span');
            iconEl.className = 'icon';
            iconEl.textContent = icon;

            const labelEl = document.createElement('span');
            labelEl.className = 'label';
            labelEl.textContent = label;

            const codeEl = document.createElement('span');
            codeEl.className = 'code';
            codeEl.textContent = name;

            btn.append(iconEl, labelEl, codeEl);
            btn.addEventListener('click', () => {
                if (send({ type: 'emotion', emotion: name })) {
                    log(`预览表情：${label} (${name})`);
                }
            });
            fragment.appendChild(btn);
        });

        el.grid.replaceChildren(fragment);
        activeButton = null;
    }

    function setActiveEmotion(emotion) {
        if (activeButton) {
            activeButton.classList.remove('active');
            activeButton = null;
        }
        if (emotion) {
            activeButton = el.grid.querySelector(`.emo[data-emotion="${emotion}"]`);
            if (activeButton) {
                activeButton.classList.add('active');
            }
        }
        el.hint.textContent = emotion
            ? '预览中：屏幕固定显示所选表情，点「跟随设备状态」退出'
            : '点击任意表情即可在屏幕上预览';
    }

    function renderActions(names) {
        const fragment = document.createDocumentFragment();

        names.filter((name) => !JOYSTICK_ACTIONS.has(name)).forEach((name) => {
            const [label, icon] = ACTION_LABELS[name] || [name, '🐾'];

            const btn = document.createElement('button');
            btn.className = 'emo';
            btn.dataset.action = name;

            const iconEl = document.createElement('span');
            iconEl.className = 'icon';
            iconEl.textContent = icon;

            const labelEl = document.createElement('span');
            labelEl.className = 'label';
            labelEl.textContent = label;

            const codeEl = document.createElement('span');
            codeEl.className = 'code';
            codeEl.textContent = name;

            btn.append(iconEl, labelEl, codeEl);
            btn.addEventListener('click', () => {
                if (send({ type: 'action', action: name })) {
                    log(`动作：${label} (${name})`);
                }
            });
            fragment.appendChild(btn);
        });

        el.actionGrid.replaceChildren(fragment);
        actionButton = null;
    }

    function applyActionState(state) {
        if (actionButton) {
            actionButton.classList.remove('active');
            actionButton = null;
        }
        if (actionTimer) {
            clearTimeout(actionTimer);
            actionTimer = null;
        }

        const current = state.current || 'idle';
        el.actionStatus.textContent = `当前动作：${current}`;
        if (current === 'idle') {
            return;
        }

        actionButton = el.actionGrid.querySelector(`.emo[data-action="${current}"]`);
        if (actionButton) {
            actionButton.classList.add('active');
        }
        actionTimer = setTimeout(() => {
            actionTimer = null;
            if (actionButton) {
                actionButton.classList.remove('active');
                actionButton = null;
            }
            el.actionStatus.textContent = '当前动作：idle';
        }, ACTION_HIGHLIGHT_MS);
    }

    function setChatState(state) {
        if (state === chatState) {
            return;
        }
        chatState = state;
        const [label, icon, busy] = CHAT_STATES[state] || [state, '❔', true];
        el.chatIco.textContent = icon;
        el.chatLabel.textContent = label;
        el.chat.classList.toggle('active', busy);
        el.chatStatus.textContent = `对话状态：${label}（${state}）`;
    }

    el.chat.addEventListener('click', () => {
        if (!send({ type: 'chat', action: 'toggle' })) {
            return;
        }
        log('对话：唤醒／打断');
        setChatState('pending');
        if (chatTimer) {
            clearTimeout(chatTimer);
        }
        chatTimer = setTimeout(() => {
            chatTimer = null;
            send({ type: 'chat' });
        }, CHAT_REQUERY_MS);
    });

    function setJoyDirection(dir) {
        if (joyDir === dir) {
            return;
        }
        joyDir = dir;

        el.joyDir.textContent = dir ? DIR_LABEL[dir] : '松开';
        el.joyRing.classList.toggle('pulse', dir !== null);

        send({ type: 'drive', dir: dir || 'none' });
    }

    function dirFromDelta(dx, dy, radius) {
        if (Math.hypot(dx, dy) < radius * JOY_DEAD_ZONE) {
            return null;
        }
        const angle = (Math.atan2(dy, dx) * 180) / Math.PI;
        if (angle >= -135 && angle < -45) {
            return 'forward';
        }
        if (angle >= -45 && angle < 45) {
            return 'right';
        }
        if (angle >= 45 && angle < 135) {
            return 'backward';
        }
        return 'left';
    }

    function updateJoystick(event) {
        const rect = el.joyBg.getBoundingClientRect();
        const radius = rect.width / 2;
        let dx = event.clientX - (rect.left + radius);
        let dy = event.clientY - (rect.top + radius);

        const distance = Math.hypot(dx, dy);
        const limit = radius - 26;
        if (distance > limit) {
            dx = (dx / distance) * limit;
            dy = (dy / distance) * limit;
        }
        el.joyThumb.style.transform = `translate(${dx}px, ${dy}px)`;
        setJoyDirection(dirFromDelta(dx, dy, radius));
    }

    function releaseJoystick() {
        joyDragging = false;
        el.joyThumb.classList.remove('grabbing');
        el.joyThumb.style.transform = '';
        setJoyDirection(null);
    }

    el.joyBg.addEventListener('pointerdown', (event) => {
        joyDragging = true;
        el.joyBg.setPointerCapture(event.pointerId);
        el.joyThumb.classList.add('grabbing');
        updateJoystick(event);
    });

    el.joyBg.addEventListener('pointermove', (event) => {
        if (joyDragging) {
            updateJoystick(event);
        }
    });

    ['pointerup', 'pointercancel'].forEach((type) => {
        el.joyBg.addEventListener(type, releaseJoystick);
    });

    el.stand.addEventListener('click', () => {
        setJoyDirection(null);
        if (send({ type: 'action', action: 'stand' })) {
            log('遥控：停下（站立）');
        }
    });

    function hex2(n) {
        return n.toString(16).padStart(2, '0');
    }

    function currentHex() {
        return `#${hex2(lamp.r)}${hex2(lamp.g)}${hex2(lamp.b)}`;
    }

    function setRgb(r, g, b) {
        lamp.r = r;
        lamp.g = g;
        lamp.b = b;
        el.r.value = r;
        el.g.value = g;
        el.b.value = b;
        el.rVal.textContent = r;
        el.gVal.textContent = g;
        el.bVal.textContent = b;
        el.color.value = currentHex();
        el.colorHex.textContent = currentHex().toUpperCase();
    }

    function applyLampState(state) {
        if (typeof state.on === 'boolean') {
            lamp.on = state.on;
        }
        if (typeof state.mode === 'number') {
            lamp.mode = state.mode;
        }
        if (typeof state.brightness === 'number') {
            lamp.brightness = state.brightness;
            el.brightness.value = state.brightness;
            el.brightnessVal.textContent = `${state.brightness}%`;
        }
        if (typeof state.speed === 'number') {
            lamp.speed = state.speed;
            el.lampSpeed.value = positionFromPercent(lamp.speed);
            el.lampSpeedVal.textContent = speedText(lamp.speed);
        }
        if (typeof state.r === 'number' && typeof state.g === 'number' && typeof state.b === 'number') {
            setRgb(state.r, state.g, state.b);
        }

        el.effects.querySelectorAll('.effect').forEach((btn) => {
            btn.classList.toggle('active', ACTION_MODE[btn.dataset.action] === lamp.mode);
        });

        el.lampStatus.textContent =
            `灯带状态：${LAMP_MODES[lamp.mode] || '未知'} · ${currentHex().toUpperCase()} · ${lamp.brightness}%`;
    }

    function sendColorSoon() {
        if (colorTimer) {
            clearTimeout(colorTimer);
        }
        colorTimer = setTimeout(() => {
            colorTimer = null;
            send({ type: 'lamp', action: 'on', r: lamp.r, g: lamp.g, b: lamp.b });
        }, SLIDER_THROTTLE);
    }

    function sendBrightnessSoon() {
        if (brightnessTimer) {
            clearTimeout(brightnessTimer);
        }
        brightnessTimer = setTimeout(() => {
            brightnessTimer = null;
            send({ type: 'lamp', action: 'brightness', value: lamp.brightness });
        }, SLIDER_THROTTLE);
    }

    el.effects.querySelectorAll('.effect').forEach((btn) => {
        btn.addEventListener('click', () => {
            const action = btn.dataset.action;
            const payload = { type: 'lamp', action };
            if (COLORED_ACTIONS.has(action)) {
                payload.r = lamp.r;
                payload.g = lamp.g;
                payload.b = lamp.b;
            }
            if (send(payload)) {
                log(`灯效：${LAMP_MODES[ACTION_MODE[action]] || action}`);
            }
        });
    });

    el.color.addEventListener('input', () => {
        const value = el.color.value;
        setRgb(parseInt(value.slice(1, 3), 16), parseInt(value.slice(3, 5), 16),
            parseInt(value.slice(5, 7), 16));
        sendColorSoon();
    });

    [['r', el.r], ['g', el.g], ['b', el.b]].forEach(([, input]) => {
        input.addEventListener('input', () => {
            setRgb(Number(el.r.value), Number(el.g.value), Number(el.b.value));
            sendColorSoon();
        });
    });

    el.brightness.addEventListener('input', () => {
        lamp.brightness = Number(el.brightness.value);
        el.brightnessVal.textContent = `${lamp.brightness}%`;
        sendBrightnessSoon();
    });

    const SPEED_MIN_PERCENT = 50;
    const SPEED_MAX_PERCENT = 200;

    function percentFromPosition(position) {
        const percent = Math.round(SPEED_MIN_PERCENT * Math.pow(4, position / 100));
        return Math.min(SPEED_MAX_PERCENT, Math.max(SPEED_MIN_PERCENT, percent));
    }

    function positionFromPercent(percent) {
        const clamped = Math.min(SPEED_MAX_PERCENT, Math.max(SPEED_MIN_PERCENT, percent));
        return Math.round((100 * Math.log(clamped / SPEED_MIN_PERCENT)) / Math.log(4));
    }

    function speedText(percent) {
        return `${(percent / 100).toFixed(1)}×`;
    }

    function applySpeedState(state) {
        if (typeof state.value !== 'number') {
            return;
        }
        actionSpeed = state.value;
        el.speed.value = positionFromPercent(actionSpeed);
        el.speedVal.textContent = speedText(actionSpeed);
    }

    function sendSpeedSoon() {
        if (speedTimer) {
            clearTimeout(speedTimer);
        }
        speedTimer = setTimeout(() => {
            speedTimer = null;
            send({ type: 'speed', value: actionSpeed });
        }, SLIDER_THROTTLE);
    }

    el.speed.addEventListener('input', () => {
        actionSpeed = percentFromPosition(Number(el.speed.value));
        el.speedVal.textContent = speedText(actionSpeed);
        sendSpeedSoon();
    });

    el.speedVal.textContent = speedText(actionSpeed);

    function sendLampSpeedSoon() {
        if (lampSpeedTimer) {
            clearTimeout(lampSpeedTimer);
        }
        lampSpeedTimer = setTimeout(() => {
            lampSpeedTimer = null;
            send({ type: 'lamp', action: 'speed', value: lamp.speed });
        }, SLIDER_THROTTLE);
    }

    el.lampSpeed.addEventListener('input', () => {
        lamp.speed = percentFromPosition(Number(el.lampSpeed.value));
        el.lampSpeedVal.textContent = speedText(lamp.speed);
        sendLampSpeedSoon();
    });

    el.lampSpeed.value = positionFromPercent(lamp.speed);
    el.lampSpeedVal.textContent = speedText(lamp.speed);

    function trimText() {
        return LEG_LABELS.map((label, i) => `${label} ${trims[i]}°`).join(' · ');
    }

    function applyTrimState(state) {
        if (Array.isArray(state.trims)) {
            for (let i = 0; i < trims.length; ++i) {
                trims[i] = Number(state.trims[i]) || 0;
                el.trims[i].value = trims[i];
                el.trimInps[i].value = trims[i];
            }
        }
        if (typeof state.max === 'number' && state.max !== trimMax) {
            trimMax = state.max;
            el.trims.forEach((slider) => {
                slider.min = -trimMax;
                slider.max = trimMax;
            });
        }
        el.trimStatus.textContent = state.dirty
            ? `舵机微调：有未保存的改动 · ${trimText()}`
            : `舵机微调：已保存 · ${trimText()}`;
    }

    el.trims.forEach((slider, index) => {
        slider.addEventListener('input', () => {
            trims[index] = Number(slider.value);
            el.trimInps[index].value = trims[index];
            trimPending = index;
            if (trimTimer) {
                clearTimeout(trimTimer);
            }
            trimTimer = setTimeout(() => {
                trimTimer = null;
                send({ type: 'trim', index: trimPending, value: trims[trimPending] });
            }, SLIDER_THROTTLE);
        });
    });

    el.trimInps.forEach((input, index) => {
        input.addEventListener('change', () => {
            if (input.value.trim() === '') {
                input.value = trims[index];
                return;
            }
            const value = Math.min(trimMax, Math.max(-trimMax, Math.round(Number(input.value))));
            if (!Number.isFinite(value)) {
                input.value = trims[index];
                return;
            }
            trims[index] = value;
            el.trims[index].value = value;
            input.value = value;
            send({ type: 'trim', index, value });
        });
    });

    el.trimSave.addEventListener('click', () => {
        if (send({ type: 'trim', action: 'save' })) {
            log('舵机微调：保存到设备');
        }
    });

    el.trimReset.addEventListener('click', () => {
        if (send({ type: 'trim', action: 'reset' })) {
            log('舵机微调：全部归零（还要点「保存到设备」才会保留到重启后）');
        }
    });

    el.tabbar.addEventListener('click', (event) => {
        const tab = event.target.closest('.tab');
        if (!tab) {
            return;
        }
        el.tabbar.querySelectorAll('.tab').forEach((item) => {
            item.classList.toggle('active', item === tab);
        });
        document.querySelectorAll('.panel').forEach((panel) => {
            panel.classList.toggle('active', panel.id === `panel-${tab.dataset.tab}`);
        });
    });

    el.auto.addEventListener('click', () => {
        if (send({ type: 'emotion', emotion: 'auto' })) {
            log('已退出预览，恢复跟随设备状态');
        }
    });

    el.clear.addEventListener('click', () => {
        el.log.replaceChildren();
    });

    document.addEventListener('visibilitychange', () => {
        if (document.hidden) {
            stopPing();
        } else if (ws && ws.readyState === WebSocket.OPEN) {
            startPing();
        }
    });

    setRgb(lamp.r, lamp.g, lamp.b);
    setChatState('unknown');
    connect();
})();
