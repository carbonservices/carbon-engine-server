# Carbon Engine Server

Headless C++20 REST API daemon for corporate ESG and statutory SEBI BRSR compliance analytics, automated extraction, vector search, and audit report generation.

## Architecture & Components

| Component | Technology | Purpose |
|---|---|---|
| **HTTP Server** | Qt 6 (`QHttpServer`, C++20) | Asynchronous non-blocking REST API |
| **Inference** | Google Gemini API | Statutory BRSR metric extraction |
| **Vector Search** | FAISS + `text-embedding-004` | 768-dim dense embedding index and retrieval |
| **Document Engine** | PDFium + Tesseract 5.3 OCR | Native PDF text extraction with OCR fallback |
| **Storage** | SQLite 3 (WAL mode) | Relational persistence for jobs, chunks, facts, and audit logs |
| **Reports** | Inja (C++ Jinja template engine) | Standalone HTML compliance report rendering |

> [!NOTE]
> **Authentication Model**: Bring-Your-Own-Key (BYOK). Clients pass their Gemini API key per request via the `X-Gemini-API-Key` header or `api_key` payload parameter. The server does not store or manage server-wide API credentials.

## Container Deployment

Prebuilt container images are published to GitHub Container Registry:
`ghcr.io/carbonservices/carbon-engine-server:latest` (or tagged by pipeline run `:run-<number>`).

### 1. Run Locally (Docker)

```bash
docker run -d \
  --name carbon-engine \
  -p 8080:8080 \
  -v carbon_data:/app/data \
  ghcr.io/carbonservices/carbon-engine-server:latest
```

Health check:
```bash
curl http://localhost:8080/api/system
```

### 2. Deploy to Google Cloud Run

```bash
gcloud run deploy carbon-engine \
  --image=ghcr.io/carbonservices/carbon-engine-server:latest \
  --platform=managed \
  --region=us-central1 \
  --allow-unauthenticated \
  --port=8080 \
  --memory=2Gi \
  --cpu=2 \
  --concurrency=80
```

### 3. Client Connection

The desktop client (`carbon-engine-client`) connects to the server via the `CARBON_ENGINE_SERVER_URL` environment variable:

```bash
# Windows (PowerShell)
$env:CARBON_ENGINE_SERVER_URL = "https://<SERVICE_URL>"
./carbon-engine-client.exe

# Linux / macOS
export CARBON_ENGINE_SERVER_URL="https://<SERVICE_URL>"
./carbon-engine-client
```

## Environment Variables

| Variable | Default | Description |
|---|---|---|
| `PORT` | `8080` | Server listening port (auto-detected on Cloud Run). |
| `CARBON_ENGINE_BIND_ALL` | `1` | Binds to `0.0.0.0` inside container environments. |
| `CARBON_ENGINE_DB_PATH` | `/app/data/ce-database.sqlite` | SQLite database file location. |
| `CARBON_ENGINE_UPLOAD_DIR` | `/app/data/uploads` | Scratch directory for uploaded documents. |
| `CARBON_ENGINE_LOG_DIR` | `/app/data/logs` | Service log directory. |
| `CARBON_ENGINE_CLOUD_ENABLED` | `1` | Enables Gemini cloud processing (`GCLOUD` mode). |
| `CARBON_ENGINE_CLOUD_MODEL` | `gemini-2.5-flash` | Default Gemini extraction model. |
| `CARBON_ENGINE_EMBD_MODEL` | `text-embedding-004` | Default Gemini embedding model. |
| `CARBON_ENGINE_THREADS` | auto | Worker thread count for CPU pipelines. |
| `TESSDATA_PREFIX` | `/app` | Directory containing `eng.traineddata` OCR language files. |

## API Reference

All endpoints return JSON responses and support global CORS. For complete request/response schemas, JSON payloads, and `curl` examples, consult [**API.md**](API.md).

| Method | Endpoint | Description | Auth / Key |
|---|---|---|---|
| `GET` | `/api/system` | Hardware telemetry, memory, queue depth, database size | Public |
| `GET` | `/logo.png` | Static branding image asset | Public |
| `GET` | `/api/mode` | View active mode (`GCLOUD`/`OFFLINE`), active models, pacing | Public |
| `POST` | `/api/mode` | Update processing mode and active models | Public |
| `POST` | `/api/config/test` | Validate Gemini connection and measure round-trip latency | `X-Gemini-API-Key` |
| `GET` / `POST` | `/api/models` | List available models from Google AI Studio | `X-Gemini-API-Key` |
| `POST` | `/api/upload` | Upload Base64 PDF files to scratch directory | Public |
| `POST` | `/api/ingest` | Enqueue documents for parsing, OCR, and vector indexing | `X-Gemini-API-Key` |
| `GET` | `/api/jobs` | Query status of an ingestion job (`?job_id=`) | Public |
| `POST` | `/api/jobs/cancel` | Cancel in-flight document processing job | Public |
| `POST` | `/api/jobs/rate_limit` | Resolve embedding rate limit state | Public |
| `GET` | `/api/documents` | List documents and chunk counts within a job (`?job_id=`) | Public |
| `POST` | `/api/extraction/start` | Trigger statutory compliance metric extraction | `X-Gemini-API-Key` |
| `GET` | `/api/extraction/status` | Extraction progress, active section, tokens (`?job_id=`) | Public |
| `POST` | `/api/extraction/cancel` | Cancel active statutory extraction pipeline | Public |
| `POST` | `/api/extraction/rate_limit` | Resolve inference rate limit state | Public |
| `GET` | `/api/extraction/sections` | Fetch extracted metric values and chunk IDs by section (`?job_id=`) | Public |
| `PUT` | `/api/extraction/sections` | Manually modify draft metric values during review | Public |
| `POST` | `/api/extraction/accept` | Finalize draft section and lock values | Public |
| `POST` | `/api/report/export` | Render standalone HTML compliance report | Public |
| `GET` | `/api/reports` | List all historical reports and audit sessions | Public |
| `DELETE` | `/api/reports` | Delete job, SQLite records, and vector index (`?job_id=`) | Public |
| `GET` | `/api/schema/brsr` | Retrieve statutory schema metadata (Principles P1-P9) | Public |

## Local Compilation (Without Docker)

Requires Ubuntu 22.04+ with Qt 6 (`qt6-base-dev`, `qt6-httpserver-dev`), OpenBLAS, and Tesseract.

```bash
cmake -B build -G "Ninja" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCARBON_ENGINE_DOWNLOAD_MODELS=OFF

cmake --build build --config Release --parallel $(nproc)

./build/__Builds/x64/bin/carbon-engine
```

## Standards & Policies

- [CONTRIBUTING.md](CONTRIBUTING.md): C++20 standards, asynchronous HTTP handlers, and PR workflows.
- [SECURITY.md](SECURITY.md): Security policy and vulnerability disclosure channels.
- [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md): Contributor Covenant v2.1.
- [LICENSE](LICENSE): GNU General Public License v3.0 (GPLv3).
