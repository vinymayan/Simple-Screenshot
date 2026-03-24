import { createSignal, onMount, onCleanup, Show } from 'solid-js';
import './App.css';

function App() {
    const [rect, setRect] = createSignal({ x: 0, y: 0, w: 0, h: 0 });
    const [startPos, setStartPos] = createSignal({ x: 0, y: 0 });
    const [initialRect, setInitialRect] = createSignal({ x: 0, y: 0, w: 0, h: 0 });

    // Modos e Ferramentas
    const [action, setAction] = createSignal('idle'); // 'idle', 'drawing', 'dragging', 'resizing'
    const [toolMode, setToolMode] = createSignal('rect'); // 'rect' ou 'lasso'
    const [lassoPoints, setLassoPoints] = createSignal<{ x: number, y: number }[]>([]);

    // Aspect Ratio
    const [selectedRatio, setSelectedRatio] = createSignal('custom'); // 'custom', '1:1', '16:9', '3:2', 'custom-input'
    const [customRatioW, setCustomRatioW] = createSignal(16);
    const [customRatioH, setCustomRatioH] = createSignal(9);

    const [dragOffset, setDragOffset] = createSignal({ x: 0, y: 0 });
    const [resizeDir, setResizeDir] = createSignal('');

    // Calcula o valor do ratio atual
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
            setAction('drawing');
            setLassoPoints([{ x: e.clientX, y: e.clientY }]);
            return;
        }

        // Modo Retângulo
        if (target.classList.contains('resize-handle')) {
            setAction('resizing');
            setResizeDir(target.dataset.dir || '');
            setStartPos({ x: e.clientX, y: e.clientY });
            setInitialRect(rect());
        } else if (target.closest('.selection-box')) {
            setAction('dragging');
            setDragOffset({ x: e.clientX - rect().x, y: e.clientY - rect().y });
        } else {
            setAction('drawing');
            setStartPos({ x: e.clientX, y: e.clientY });
            setRect({ x: e.clientX, y: e.clientY, w: 0, h: 0 });
        }
    };

    const handleMouseMove = (e: MouseEvent) => {
        if (action() === 'drawing') {
            if (toolMode() === 'lasso') {
                setLassoPoints([...lassoPoints(), { x: e.clientX, y: e.clientY }]);
            } else {
                let newW = Math.abs(e.clientX - startPos().x);
                let newH = Math.abs(e.clientY - startPos().y);
                let newX = Math.min(startPos().x, e.clientX);
                let newY = Math.min(startPos().y, e.clientY);

                const ratio = getRatioValue();
                if (ratio) {
                    // Mantém a proporção baseada no maior movimento
                    if (newW / ratio > newH) {
                        newH = newW / ratio;
                        if (e.clientY < startPos().y) newY = startPos().y - newH;
                    } else {
                        newW = newH * ratio;
                        if (e.clientX < startPos().x) newX = startPos().x - newW;
                    }
                }
                setRect({ x: newX, y: newY, w: newW, h: newH });
            }
        } else if (action() === 'dragging' && toolMode() === 'rect') {
            setRect((prev) => ({
                ...prev,
                x: e.clientX - dragOffset().x,
                y: e.clientY - dragOffset().y,
            }));
        } else if (action() === 'resizing' && toolMode() === 'rect') {
            const dir = resizeDir();
            const { x, y, w, h } = initialRect();
            const dx = e.clientX - startPos().x;
            const dy = e.clientY - startPos().y;

            let newW = w;
            let newH = h;
            let newX = x;
            let newY = y;

            if (dir.includes('e')) newW = w + dx;
            if (dir.includes('s')) newH = h + dy;
            if (dir.includes('w')) { newW = w - dx; newX = x + dx; }
            if (dir.includes('n')) { newH = h - dy; newY = y + dy; }

            const ratio = getRatioValue();
            if (ratio) {
                // Ajustes de eixo para manter a proporção ao puxar bordas
                if (dir === 'n' || dir === 's') {
                    const adjW = newH * ratio;
                    newX -= (adjW - newW) / 2;
                    newW = adjW;
                } else if (dir === 'e' || dir === 'w') {
                    const adjH = newW / ratio;
                    newY -= (adjH - newH) / 2;
                    newH = adjH;
                } else {
                    // Ajuste de cantos
                    if (newW / ratio > newH) {
                        const adjH = newW / ratio;
                        if (dir.includes('n')) newY -= (adjH - Math.abs(newH));
                        newH = adjH;
                    } else {
                        const adjW = newH * ratio;
                        if (dir.includes('w')) newX -= (adjW - Math.abs(newW));
                        newW = adjW;
                    }
                }
            }

            // Impede larguras/alturas negativas
            if (newW < 10) newW = 10;
            if (newH < 10) newH = 10;

            setRect({ x: newX, y: newY, w: newW, h: newH });
        }
    };

    const handleMouseUp = () => {
        setAction('idle');
    };

    onMount(() => {
        window.addEventListener('mouseup', handleMouseUp);
    });

    onCleanup(() => {
        window.removeEventListener('mouseup', handleMouseUp);
    });

    const selectFullScreen = () => {
        setToolMode('rect');
        setRect({ x: 0, y: 0, w: window.innerWidth, h: window.innerHeight });
    };

    const fechar = () => {
        // @ts-ignore
        if (window.hideWindow) window.hideWindow();
        setRect({ x: 0, y: 0, w: 0, h: 0 });
        setLassoPoints([]);
    };

    const capturar = (comUI: boolean) => {
        let cx = 0, cy = 0, cw = 0, ch = 0;
        let lassoStr = ""; // Nova string para armazenar as coordenadas do polígono

        if (toolMode() === 'rect') {
            const { x, y, w, h } = rect();
            if (w === 0 || h === 0) return alert("Selecione uma área primeiro!");
            cx = x; cy = y; cw = w; ch = h;
        } else {
            if (lassoPoints().length < 3) return alert("Desenhe uma área com o laço!");
            const xs = lassoPoints().map(p => p.x);
            const ys = lassoPoints().map(p => p.y);
            cx = Math.min(...xs);
            cy = Math.min(...ys);
            cw = Math.max(...xs) - cx;
            ch = Math.max(...ys) - cy;

            // Converte os pontos para string no formato: x1,y1;x2,y2;x3,y3...
            lassoStr = lassoPoints().map(p => `${Math.round(p.x)},${Math.round(p.y)}`).join(';');
        }

        // Novo formato: comUI | X | Y | W | H | p1_x,p1_y;p2_x,p2_y...
        const dataString = `${comUI ? 1 : 0}|${Math.round(cx)}|${Math.round(cy)}|${Math.round(cw)}|${Math.round(ch)}|${lassoStr}`;

        fechar();

        setTimeout(() => {
            // @ts-ignore
            if (window.takeScreenshot) {
                // @ts-ignore
                window.takeScreenshot(dataString);
            }
        }, 50);
    };

    // Calcula o desenho da máscara com o recorte (furo) embutido
    const getMaskPath = () => {
        // Usa um tamanho bem grande para garantir que cobre tudo
        const screenW = window.innerWidth;
        const screenH = window.innerHeight;

        // Caminho 1: Cobre a tela toda
        let path = `M 0 0 H ${screenW} V ${screenH} H 0 Z`;

        // Caminho 2: Faz o recorte (o fill-rule="evenodd" entende isso como um buraco)
        if (toolMode() === 'rect' && rect().w > 0) {
            const { x, y, w, h } = rect();
            path += ` M ${x} ${y} H ${x + w} V ${y + h} H ${x} Z`;
        } else if (toolMode() === 'lasso' && lassoPoints().length > 0) {
            const points = lassoPoints();
            path += ` M ${points[0].x} ${points[0].y}`;
            for (let i = 1; i < points.length; i++) {
                path += ` L ${points[i].x} ${points[i].y}`;
            }
            path += ' Z';
        }
        return path;
    };

    return (
        <div class="overlay-container" onMouseDown={handleMouseDown} onMouseMove={handleMouseMove}>

            {/* Máscara de Recorte usando SVG (1000x mais leve e suporta polígonos) */}
            <svg class="mask-svg">
                <path d={getMaskPath()} fill="rgba(0,0,0,0.5)" fill-rule="evenodd" />

                {/* Linha do Laço (borda) */}
                <Show when={toolMode() === 'lasso' && lassoPoints().length > 0}>
                    <polyline points={lassoPoints().map(p => `${p.x},${p.y}`).join(' ')} fill="none" stroke="#00a8ff" stroke-width="2" />
                </Show>
            </svg>

            {/* Barra de Ferramentas */}
            <div class="toolbar">
                <div class="tool-group">
                    <button class={toolMode() === 'rect' ? 'active' : ''} onClick={() => setToolMode('rect')}>Quadrado</button>
                    <button class={toolMode() === 'lasso' ? 'active' : ''} onClick={() => setToolMode('lasso')}>Livre</button>
                </div>

                <Show when={toolMode() === 'rect'}>
                    <div class="tool-group">
                        <select onChange={(e) => setSelectedRatio(e.target.value)} value={selectedRatio()}>
                            <option value="custom">Livre</option>
                            <option value="1:1">1:1</option>
                            <option value="3:2">3:2</option>
                            <option value="4:3">4:3</option>
                            <option value="16:9">16:9</option>
                            <option value="custom-input">Personalizado</option>
                        </select>

                        <Show when={selectedRatio() === 'custom-input'}>
                            <input type="number" value={customRatioW()} onInput={(e) => setCustomRatioW(Number(e.target.value))} class="ratio-input" min="1" />
                            <span>:</span>
                            <input type="number" value={customRatioH()} onInput={(e) => setCustomRatioH(Number(e.target.value))} class="ratio-input" min="1" />
                        </Show>
                    </div>
                </Show>

                <div class="tool-group">
                    <button onClick={() => capturar(true)}>Com UI</button>
                    <button onClick={() => capturar(false)}>Sem UI</button>
                    <button onClick={selectFullScreen}>Tela Toda</button>
                    <button onClick={() => { setRect({ x: 0, y: 0, w: 0, h: 0 }); setLassoPoints([]); }}>Limpar</button>
                    <button class="close-btn" onClick={fechar}>Cancelar</button>
                </div>
            </div>

            {/* Caixa de Seleção do Modo Retângulo com todos os Handles */}
            <Show when={toolMode() === 'rect' && rect().w > 0 && rect().h > 0}>
                <div class="selection-box" style={{ left: `${rect().x}px`, top: `${rect().y}px`, width: `${rect().w}px`, height: `${rect().h}px` }}>
                    {/* Cantos */}
                    <div class="resize-handle nw" data-dir="nw"></div>
                    <div class="resize-handle ne" data-dir="ne"></div>
                    <div class="resize-handle sw" data-dir="sw"></div>
                    <div class="resize-handle se" data-dir="se"></div>
                    {/* Laterais */}
                    <div class="resize-handle n" data-dir="n"></div>
                    <div class="resize-handle s" data-dir="s"></div>
                    <div class="resize-handle w" data-dir="w"></div>
                    <div class="resize-handle e" data-dir="e"></div>
                </div>
            </Show>
        </div>
    );
}

export default App;