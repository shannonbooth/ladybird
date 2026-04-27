(function () {
    "use strict";

    const bridge = window.__ladybirdPDFViewer;
    const statusElement = document.getElementById("status");
    const pagesElement = document.getElementById("pages");
    const zoomLabelElement = document.getElementById("zoom-label");
    const zoomOutButton = document.getElementById("zoom-out");
    const zoomInButton = document.getElementById("zoom-in");

    const state = {
        pdfDocument: null,
        scale: 1.1,
        isRendering: false,
    };

    function setStatus(text, isError) {
        statusElement.textContent = text;
        statusElement.classList.toggle("is-error", Boolean(isError));
    }

    function updateZoomLabel() {
        zoomLabelElement.textContent = Math.round(state.scale * 100) + "%";
    }

    function setControlsDisabled(disabled) {
        zoomOutButton.disabled = disabled;
        zoomInButton.disabled = disabled;
    }

    function clearPages() {
        while (pagesElement.firstChild)
            pagesElement.firstChild.remove();
    }

    function defaultTitle() {
        if (!bridge || typeof bridge.getURL !== "function")
            return "PDF";

        try {
            const url = new URL(bridge.getURL());
            return url.href;
        } catch {
            return bridge.getURL() || "PDF";
        }
    }

    async function renderPage(pageNumber) {
        const page = await state.pdfDocument.getPage(pageNumber);
        const viewport = page.getViewport({ scale: state.scale });

        const pageContainer = document.createElement("section");
        pageContainer.className = "page";

        const pageLabel = document.createElement("div");
        pageLabel.className = "page-label";
        pageLabel.textContent = "Page " + pageNumber + " / " + state.pdfDocument.numPages;

        const canvas = document.createElement("canvas");
        canvas.width = Math.ceil(viewport.width);
        canvas.height = Math.ceil(viewport.height);

        pageContainer.append(pageLabel, canvas);
        pagesElement.append(pageContainer);

        const context = canvas.getContext("2d");
        await page.render({
            canvasContext: context,
            viewport,
        }).promise;
    }

    async function renderDocument() {
        if (!state.pdfDocument || state.isRendering)
            return;

        state.isRendering = true;
        setControlsDisabled(true);
        clearPages();
        setStatus("Rendering " + state.pdfDocument.numPages + " page(s)...");

        try {
            for (let pageNumber = 1; pageNumber <= state.pdfDocument.numPages; ++pageNumber)
                await renderPage(pageNumber);

            setStatus("Loaded " + state.pdfDocument.numPages + " page(s).");
        } catch (error) {
            console.error(error);
            setStatus("Failed to render PDF: " + error.message, true);
        } finally {
            state.isRendering = false;
            setControlsDisabled(false);
        }
    }

    async function loadPdfDocument() {
        if (!bridge || typeof bridge.getData !== "function") {
            setStatus("PDF viewer bridge is unavailable.", true);
            setControlsDisabled(true);
            return;
        }

        if (!window.pdfjsLib) {
            setStatus("pdf.js failed to load.", true);
            setControlsDisabled(true);
            return;
        }

        updateZoomLabel();
        setStatus("Loading PDF...");

        try {
            const buffer = bridge.getData();
            const loadingTask = window.pdfjsLib.getDocument({ data: new Uint8Array(buffer) });
            state.pdfDocument = await loadingTask.promise;
            document.title = defaultTitle();

            try {
                const metadata = await state.pdfDocument.getMetadata();
                const title = metadata && metadata.info && metadata.info.Title;
                if (title)
                    document.title = title;
            } catch {
            }

            await renderDocument();
        } catch (error) {
            console.error(error);
            setStatus("Failed to load PDF: " + error.message, true);
            setControlsDisabled(true);
        }
    }

    zoomOutButton.addEventListener("click", async function () {
        if (!state.pdfDocument || state.isRendering)
            return;
        state.scale = Math.max(0.5, state.scale - 0.1);
        updateZoomLabel();
        await renderDocument();
    });

    zoomInButton.addEventListener("click", async function () {
        if (!state.pdfDocument || state.isRendering)
            return;
        state.scale = Math.min(3, state.scale + 0.1);
        updateZoomLabel();
        await renderDocument();
    });

    loadPdfDocument();
})();
