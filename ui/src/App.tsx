import { createSignal, onMount, onCleanup, Show, createEffect } from 'solid-js'; 
import './App.css';

function App() {
    const [rect, setRect] = createSignal({ x: 0, y: 0, w: 0, h: 0 });
    const [startPos, setStartPos] = createSignal({ x: 0, y: 0 });
    const [initialRect, setInitialRect] = createSignal({ x: 0, y: 0, w: 0, h: 0 });

    const [action, setAction] = createSignal('idle');
    const [toolMode, setToolMode] = createSignal('rect');
    const [lassoPoints, setLassoPoints] = createSignal<{ x: number, y: number }[]>([]);

    const [selectedRatio, setSelectedRatio] = createSignal('custom');
    const [customRatioW, setCustomRatioW] = createSignal(16);
    const [customRatioH, setCustomRatioH] = createSignal(9);

    const [dragOffset, setDragOffset] = createSignal({ x: 0, y: 0 });
    const [resizeDir, setResizeDir] = createSignal('');
    const [withUIKeyText, setWithUIKeyText] = createSignal("");
    const [noUIKeyText, setNoUIKeyText] = createSignal("");

    createEffect(() => {
        let cx = 0, cy = 0, cw = 0, ch = 0;
        let lassoStr = "";
        if (toolMode() === 'rect') {
            const { x, y, w, h } = rect();
            cx = x; cy = y; cw = w; ch = h;
        } else {
            if (lassoPoints().length >= 3) {
                const xs = lassoPoints().map(p => p.x);
                const ys = lassoPoints().map(p => p.y);
                cx = Math.min(...xs); cy = Math.min(...ys);
                cw = Math.max(...xs) - cx; ch = Math.max(...ys) - cy;
                lassoStr = lassoPoints().map(p => `${Math.round(p.x)},${Math.round(p.y)}`).join(';');
            }
        }
        const dataString = `0|${Math.round(cx)}|${Math.round(cy)}|${Math.round(cw)}|${Math.round(ch)}|${lassoStr}`;
        // @ts-ignore
        if (window.syncCropData) window.syncCropData(dataString);
    });

    const getRatioValue = () => {
        if (selectedRatio() === 'custom') return null;
        if (selectedRatio() === 'custom-input') return customRatioW() / customRatioH();
        const [w, h] = selectedRatio().split(':').map(Number);
        return w / h;
    };

    const handleMouseDown = (e: MouseEvent) => { 
        const target = e.target as HTMLElement;
        if (target.closest('.toolbar')) return;
        if (toolMode() === 'lasso') {
            setAction('drawing'); setLassoPoints([{ x: e.clientX, y: e.clientY }]); return;
        }
        if (target.classList.contains('resize-handle')) {
            setAction('resizing'); setResizeDir(target.dataset.dir || '');
            setStartPos({ x: e.clientX, y: e.clientY }); setInitialRect(rect());
        } else if (target.closest('.selection-box')) {
            setAction('dragging'); setDragOffset({ x: e.clientX - rect().x, y: e.clientY - rect().y });
        } else {
            setAction('drawing'); setStartPos({ x: e.clientX, y: e.clientY });
            setRect({ x: e.clientX, y: e.clientY, w: 0, h: 0 });
        }
    };
    const handleMouseMove = (e: MouseEvent) => { 
        if (action() === 'drawing') {
            if (toolMode() === 'lasso') {
                setLassoPoints([...lassoPoints(), { x: e.clientX, y: e.clientY }]);
            } else {
                let newW = Math.abs(e.clientX - startPos().x); let newH = Math.abs(e.clientY - startPos().y);
                let newX = Math.min(startPos().x, e.clientX); let newY = Math.min(startPos().y, e.clientY);
                const ratio = getRatioValue();
                if (ratio) {
                    if (newW / ratio > newH) { newH = newW / ratio; if (e.clientY < startPos().y) newY = startPos().y - newH; }
                    else { newW = newH * ratio; if (e.clientX < startPos().x) newX = startPos().x - newW; }
                }
                setRect({ x: newX, y: newY, w: newW, h: newH });
            }
        } else if (action() === 'dragging' && toolMode() === 'rect') {
            setRect((prev) => ({ ...prev, x: e.clientX - dragOffset().x, y: e.clientY - dragOffset().y, }));
        } else if (action() === 'resizing' && toolMode() === 'rect') {
            const dir = resizeDir(); const { x, y, w, h } = initialRect();
            const dx = e.clientX - startPos().x; const dy = e.clientY - startPos().y;
            let newW = w; let newH = h; let newX = x; let newY = y;
            if (dir.includes('e')) newW = w + dx; if (dir.includes('s')) newH = h + dy;
            if (dir.includes('w')) { newW = w - dx; newX = x + dx; }
            if (dir.includes('n')) { newH = h - dy; newY = y + dy; }
            const ratio = getRatioValue();
            if (ratio) {
                if (dir === 'n' || dir === 's') { const adjW = newH * ratio; newX -= (adjW - newW) / 2; newW = adjW; }
                else if (dir === 'e' || dir === 'w') { const adjH = newW / ratio; newY -= (adjH - newH) / 2; newH = adjH; }
                else {
                    if (newW / ratio > newH) { const adjH = newW / ratio; if (dir.includes('n')) newY -= (adjH - Math.abs(newH)); newH = adjH; }
                    else { const adjW = newH * ratio; if (dir.includes('w')) newX -= (adjW - Math.abs(newW)); newW = adjW; }
                }
            }
            if (newW < 10) newW = 10; if (newH < 10) newH = 10;
            setRect({ x: newX, y: newY, w: newW, h: newH });
        }
    };

    const handleMouseUp = () => setAction('idle');

    const selectFullScreen = () => {
        setToolMode('rect');
        setRect({ x: 0, y: 0, w: window.innerWidth, h: window.innerHeight });
    };

    onMount(() => {
        window.addEventListener('mouseup', handleMouseUp);

        const hash = decodeURIComponent(window.location.hash.replace('#', ''));
        if (hash) {
            const parts = hash.split('|'); 
            if (parts.length >= 3) {
                const w = Number(parts[0]);
                const h = Number(parts[1]);
                const useCustom = parts[2];

                if (useCustom === '1' && w > 0 && h > 0) {
                    const sw = window.innerWidth; const sh = window.innerHeight;
                    setRect({ x: (sw - w) / 2, y: (sh - h) / 2, w: w, h: h });
                } else {
                    selectFullScreen();
                }

                if (parts[3]) setWithUIKeyText(parts[3]);
                if (parts[4]) setNoUIKeyText(parts[4]);
            } else {
                selectFullScreen();
            }
        } else {
            selectFullScreen();
        }
    });

    onCleanup(() => { window.removeEventListener('mouseup', handleMouseUp); });

    const fechar = () => {
        // @ts-ignore
        if (window.hideWindow) window.hideWindow();
    };

    const capturar = (comUI: boolean) => {
        let cx = 0, cy = 0, cw = 0, ch = 0; let lassoStr = "";
        if (toolMode() === 'rect') {
            const { x, y, w, h } = rect();
            if (w === 0 || h === 0) return alert("Select an area first!");
            cx = x; cy = y; cw = w; ch = h;
        } else {
            if (lassoPoints().length < 3) return alert("Draw an area with the lasso!");
            const xs = lassoPoints().map(p => p.x); const ys = lassoPoints().map(p => p.y);
            cx = Math.min(...xs); cy = Math.min(...ys);
            cw = Math.max(...xs) - cx; ch = Math.max(...ys) - cy;
            lassoStr = lassoPoints().map(p => `${Math.round(p.x)},${Math.round(p.y)}`).join(';');
        }
        const dataString = `${comUI ? 1 : 0}|${Math.round(cx)}|${Math.round(cy)}|${Math.round(cw)}|${Math.round(ch)}|${lassoStr}`;
        fechar();
        setTimeout(() => {
            // @ts-ignore
            if (window.takeScreenshot) window.takeScreenshot(dataString);
        }, 50);
    };

    const getMaskPath = () => { 
        const screenW = window.innerWidth; const screenH = window.innerHeight;
        let path = `M 0 0 H ${screenW} V ${screenH} H 0 Z`;
        if (toolMode() === 'rect' && rect().w > 0) {
            const { x, y, w, h } = rect(); path += ` M ${x} ${y} H ${x + w} V ${y + h} H ${x} Z`;
        } else if (toolMode() === 'lasso' && lassoPoints().length > 0) {
            const points = lassoPoints(); path += ` M ${points[0].x} ${points[0].y}`;
            for (let i = 1; i < points.length; i++) path += ` L ${points[i].x} ${points[i].y}`;
            path += ' Z';
        }
        return path;
    };

    return (
        <div class="overlay-container" onMouseDown={handleMouseDown} onMouseMove={handleMouseMove}>
            <svg class="mask-svg">
                <path d={getMaskPath()} fill="rgba(0,0,0,0.5)" fill-rule="evenodd" />
                <Show when={toolMode() === 'lasso' && lassoPoints().length > 0}>
                    <polyline points={lassoPoints().map(p => `${p.x},${p.y}`).join(' ')} fill="none" stroke="#00a8ff" stroke-width="2" />
                </Show>
            </svg>

            <div class="toolbar">
                <div class="tool-group">
                    <button class={toolMode() === 'rect' ? 'active' : ''} onClick={() => setToolMode('rect')}>Rectangle</button>
                    <button class={toolMode() === 'lasso' ? 'active' : ''} onClick={() => setToolMode('lasso')}>Freehand</button>
                </div>
                <Show when={toolMode() === 'rect'}>
                    <div class="tool-group">
                        <select onChange={(e) => setSelectedRatio(e.target.value)} value={selectedRatio()}>
                            <option value="custom">Free</option>
                            <option value="1:1">1:1</option>
                            <option value="3:2">3:2</option>
                            <option value="4:3">4:3</option>
                            <option value="16:9">16:9</option>
                            <option value="custom-input">Custom</option>
                        </select>
                        <Show when={selectedRatio() === 'custom-input'}>
                            <input type="number" value={customRatioW()} onInput={(e) => setCustomRatioW(Number(e.target.value))} class="ratio-input" min="1" />
                            <span>:</span>
                            <input type="number" value={customRatioH()} onInput={(e) => setCustomRatioH(Number(e.target.value))} class="ratio-input" min="1" />
                        </Show>
                    </div>
                </Show>
                <div class="tool-group">
                    <button onClick={() => capturar(true)}>
                        With UI <Show when={withUIKeyText()}><span class="keybind-hint">[{withUIKeyText()}]</span></Show>
                    </button>
                    <button onClick={() => capturar(false)}>
                        No UI <Show when={noUIKeyText()}><span class="keybind-hint">[{noUIKeyText()}]</span></Show>
                    </button>
                    <button onClick={selectFullScreen}>Fullscreen</button>
                    <button onClick={() => { setRect({ x: 0, y: 0, w: 0, h: 0 }); setLassoPoints([]); }}>Clear</button>
                    <button class="close-btn" onClick={fechar}>Cancel</button>
                </div>
            </div>

            <Show when={toolMode() === 'rect' && rect().w > 0 && rect().h > 0}>
                <div class="selection-box" style={{ left: `${rect().x}px`, top: `${rect().y}px`, width: `${rect().w}px`, height: `${rect().h}px` }}>
                    <div class="resize-handle nw" data-dir="nw"></div> <div class="resize-handle ne" data-dir="ne"></div>
                    <div class="resize-handle sw" data-dir="sw"></div> <div class="resize-handle se" data-dir="se"></div>
                    <div class="resize-handle n" data-dir="n"></div> <div class="resize-handle s" data-dir="s"></div>
                    <div class="resize-handle w" data-dir="w"></div> <div class="resize-handle e" data-dir="e"></div>
                </div>
            </Show>
        </div>
    );
}

export default App;