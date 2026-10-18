# Carbon Engine Server

Native C++20 Headless REST API Server for Corporate ESG & BRSR Compliance Analytics.

This repository contains the `carbon-engine` server daemon packaged as a lightweight container image for deployment on **Google Cloud Run**, AWS ECS, or Kubernetes.

---

## Table of Contents

- [Overview & Architecture](#overview--architecture)
- [Quick Start with Docker](#quick-start-with-docker)
- [Google Cloud Run Deployment Guide](#google-cloud-run-deployment-guide)
  - [Prerequisites](#prerequisites)
  - [1. Build & Push Container Image](#1-build--push-container-image)
  - [2. Deploy Service to Cloud Run](#2-deploy-service-to-cloud-run)
  - [3. Verify Cloud Run Deployment](#3-verify-cloud-run-deployment)
  - [4. Connecting Clients to Cloud Run](#4-connecting-clients-to-cloud-run)
- [Environment Variables](#environment-variables)
- [Supported REST APIs](#supported-rest-apis)
- [API Reference Guide (API.md)](API.md)
- [Local Compilation (Without Docker)](#local-compilation-without-docker)
- [License](#license)

---

## Overview & Architecture

`carbon-engine` is an asynchronous, headless C++20 REST server designed for corporate ESG compliance, statutory SEBI BRSR metric extraction, and audit report generation.

### Core Stack

| Component | Technology | Purpose |
|---|---|---|
| **Core Runtime** | C++20, Qt 6 (Core, HttpServer, Network, Sql) | High-performance asynchronous REST API server |
| **LLM Inference** | Google Gemini API (`gemini-2.5-flash`, `gemini-2.0-flash`) | Statutory ESG metric extraction and schema validation |
| **Vector Search** | `text-embedding-004` + FAISS C++ | Dense vector embeddings and semantic context retrieval |
| **Document Engine** | PDFium + Tesseract 5.3 OCR | Native PDF text extraction with OCR image fallback |
| **Data Persistence** | SQLite 3 (WAL mode) | Relational persistence for jobs, chunks, facts, and audit logs |
| **Report Generator** | Inja (C++ Jinja-compatible template engine) | Standalone HTML compliance reports with provenance anchors |
| **Container Target** | Multi-stage Docker image (121 MB runtime) | Optimized for Google Cloud Run, AWS ECS, and Kubernetes |

### Key Architectural Attributes

| Attribute | Specification | Operational Benefit |
|---|---|---|
| **Cost Model** | Bring Your Own Key (`X-Gemini-API-Key` header / payload) | Zero server-side LLM costs; users pay for their own tokens |
| **Compliance Scope** | SEBI BRSR Principles P1–P9, General & Management Disclosures | Strict schema enforcement with automated JSON repair |
| **Memory Footprint** | ~56 MB idle process RAM | Runs cost-effectively on small Cloud Run pods (512 MB – 1 GB) |
| **Observability** | Mirrored stdout/stderr + `/app/data/logs/*.log` | Real-time telemetry ingestion into Google Cloud Logging |
| **Cloud Run Contract** | Auto-detects `$PORT`, listens on `0.0.0.0`, `/api/system` health check | Native zero-configuration serverless auto-scaling |

---

## Quick Start with Docker

### 1. Build the Docker Image
Inside WSL / Linux:
```bash
docker build -t carbon-engine-server:latest .
```

### 2. Run Locally in Docker
```bash
docker run -d \
  --name carbon-engine \
  -p 8080:8080 \
  -v carbon_data:/app/data \
  carbon-engine-server:latest
```

### 3. Verify Health Check
```bash
curl http://localhost:8080/api/system
```

---

## Google Cloud Run Deployment Guide

### Prerequisites
- [Google Cloud SDK (`gcloud`)](https://cloud.google.com/sdk/docs/install) installed and authenticated.
- A Google Cloud Project with Cloud Run and Artifact Registry APIs enabled.

### 1. Build & Push Container Image
Set your GCP project ID and region:
```bash
export PROJECT_ID="your-gcp-project-id"
export REGION="us-central1"
export REPO_NAME="carbon-repo"
export IMAGE_TAG="us-central1-docker.pkg.dev/${PROJECT_ID}/${REPO_NAME}/carbon-engine:latest"

# Create Artifact Registry Docker repository (if not already existing)
gcloud artifacts repositories create ${REPO_NAME} \
    --repository-format=docker \
    --location=${REGION} \
    --description="Carbon Engine Container Repository"

# Configure Docker credentials for GCP Artifact Registry
gcloud auth configure-docker ${REGION}-docker.pkg.dev

# Build and tag image
docker build -t ${IMAGE_TAG} .

# Push image to Artifact Registry
docker push ${IMAGE_TAG}
```

### 2. Deploy Service to Cloud Run
Deploy the container with 2 vCPUs, 2 GiB memory, and dynamic port configuration:
```bash
gcloud run deploy carbon-engine \
    --image=${IMAGE_TAG} \
    --platform=managed \
    --region=${REGION} \
    --allow-unauthenticated \
    --memory=2Gi \
    --cpu=2 \
    --concurrency=80 \
    --min-instances=0 \
    --max-instances=10 \
    --set-env-vars=CARBON_ENGINE_CLOUD_ENABLED=1,CARBON_ENGINE_BIND_ALL=1,CARBON_ENGINE_CLOUD_MODEL=gemini-2.5-flash
```

### 3. Verify Cloud Run Deployment
Once deployed, retrieve your public HTTPS service URL:
```bash
export SERVICE_URL=$(gcloud run services describe carbon-engine --platform=managed --region=${REGION} --format='value(status.url)')
echo "Service URL: ${SERVICE_URL}"

# Query live system telemetry
curl "${SERVICE_URL}/api/system"

# Test Gemini inference connection with client key
curl -H "X-Gemini-API-Key: YOUR_GEMINI_KEY" -X POST "${SERVICE_URL}/api/config/test"
```

### 4. Connecting Clients to Cloud Run

Clients communicate with the engine via standard HTTP REST requests.

To point the desktop client (`carbon-engine-client`) to your Cloud Run deployment, set the `CARBON_ENGINE_SERVER_URL` environment variable before launching:

**Windows (PowerShell):**
```powershell
$env:CARBON_ENGINE_SERVER_URL = "https://carbon-engine-xxxxx-uc.a.run.app"
./carbon-engine-client.exe
```

**Linux / macOS:**
```bash
export CARBON_ENGINE_SERVER_URL="https://carbon-engine-xxxxx-uc.a.run.app"
./carbon-engine-client
```

Any custom client (Python, Node.js, cURL, Web UI) can make direct HTTP calls to `SERVICE_URL` using the endpoints documented in [API.md](API.md).

---

## Environment Variables

| Variable | Default | Description |
|---|---|---|
| `PORT` | `8080` | Port for the HTTP server. Auto-detected in Cloud Run. |
| `CARBON_ENGINE_PORT` | `8080` | Fallback port override if `PORT` is not defined. |
| `CARBON_ENGINE_BIND_ALL` | `1` in Docker | Set to `1` to listen on `0.0.0.0` (all interfaces). Auto-enabled on Cloud Run (`K_SERVICE`) and Docker (`/.dockerenv`). |
| `CARBON_ENGINE_DB_PATH` | `/app/data/ce-database.sqlite` | Absolute path to the persistent SQLite database file. |
| `CARBON_ENGINE_UPLOAD_DIR` | `/app/data/uploads` | Scratch directory where uploaded ingestion files are saved. |
| `CARBON_ENGINE_LOG_DIR` | `/app/data/logs` | Directory where daily service logs are stored. |
| `CARBON_ENGINE_CLOUD_ENABLED`| `1` | Enables Cloud Mode (`GCLOUD`) by default. |
| `CARBON_ENGINE_CLOUD_MODEL` | `gemini-2.5-flash` | Default Gemini model name for extraction. |
| `CARBON_ENGINE_EMBD_MODEL` | `text-embedding-004` | Embedding model for semantic vector searches. |
| `CARBON_ENGINE_THREADS` | auto (2-8) | Number of worker threads for ingestion / CPU execution. |
| `TESSDATA_PREFIX` | `/app` | Path to directory containing `eng.traineddata`. |

---

## Supported REST APIs

All endpoints return JSON responses and support global CORS (`Access-Control-Allow-Origin: *`, `Access-Control-Allow-Headers: Content-Type, Authorization, X-Gemini-API-Key`).

> 📖 **Full Specification**: For complete JSON request/response payloads, field types, and `curl` command examples, consult the [**REST API Specification Guide (API.md)**](API.md).

### API Reference Table

| Category | Method | Endpoint | Description | Auth / Key |
|---|---|---|---|---|
| **System & Health** | `GET` | `/api/system` | Hardware telemetry, database size, queue depth, engine phase | Public |
| **System & Health** | `GET` | `/logo.png` | Static branding asset image | Public |
| **Configuration** | `GET` | `/api/mode` | View active mode (`GCLOUD` / `OFFLINE`), active models & pacing | Public |
| **Configuration** | `POST` | `/api/mode` | Set processing mode and active models | Public |
| **Configuration** | `POST` | `/api/config/test` | Validate Gemini connection and measure round-trip latency | `X-Gemini-API-Key` / Body |
| **Configuration** | `GET` / `POST` | `/api/models` | List available models from Google AI Studio | `X-Gemini-API-Key` / Body |
| **Ingestion** | `POST` | `/api/upload` | Upload PDF files (Base64) to scratch directory | Public |
| **Ingestion** | `POST` | `/api/ingest` | Enqueue documents for parsing, OCR, and vector indexing | `X-Gemini-API-Key` / Body |
| **Ingestion** | `GET` | `/api/jobs` | Query status of an ingestion job (`PENDING`, `PROCESSING`, `COMPLETED`, `FAILED`) | Query `?job_id=` |
| **Ingestion** | `POST` | `/api/jobs/cancel` | Cancel in-flight document processing job | Body `job_id` |
| **Ingestion** | `POST` | `/api/jobs/rate_limit` | Resolve embedding rate limit (`retry`, `switch_model`, `switch_local`, `cancel`) | Body `job_id` |
| **Ingestion** | `GET` | `/api/documents` | List documents and chunk counts within a job | Query `?job_id=` |
| **Extraction** | `POST` | `/api/extraction/start` | Trigger statutory compliance metric extraction for a job | `X-Gemini-API-Key` / Body |
| **Extraction** | `GET` | `/api/extraction/status` | Live extraction progress, active section, tokens, and missing fields | Query `?job_id=` |
| **Extraction** | `POST` | `/api/extraction/cancel` | Cancel active statutory extraction pipeline | Body `job_id` |
| **Extraction** | `POST` | `/api/extraction/rate_limit` | Resolve inference rate limit (`retry`, `switch_model`, `switch_local`, `cancel`) | Body `job_id` |
| **Audit Review** | `GET` | `/api/extraction/sections` | Fetch extracted metric values and source chunk IDs by section | Query `?job_id=` |
| **Audit Review** | `PUT` | `/api/extraction/sections` | Manually modify extracted draft metric values during review | Body |
| **Audit Review** | `POST` | `/api/extraction/accept` | Finalize draft section and lock values into verified state | Body |
| **Reports** | `POST` | `/api/report/export` | Render standalone HTML compliance report with Inja templates | Body |
| **Reports** | `GET` | `/api/reports` | List all historical reports and audit sessions | Public |
| **Reports** | `DELETE`| `/api/reports` | Delete report, SQLite records, vector index, and cached facts | Query `?job_id=` |
| **Catalog** | `GET` | `/api/schema/brsr` | Retrieve statutory schema metadata (Principles P1–P9, metrics) | Public |

---

## Local Compilation (Without Docker)

On Ubuntu 22.04 / 24.04:

```bash
sudo apt-get update && sudo apt-get install -y \
  build-essential ninja-build cmake git curl \
  qt6-base-dev qt6-httpserver-dev \
  libopenblas-dev liblapack-dev \
  libtesseract-dev libleptonica-dev

# Configure and compile
cmake -B build -G "Ninja" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCARBON_ENGINE_DOWNLOAD_MODELS=OFF

cmake --build build --config Release --parallel $(nproc)

# Execute
./build/__Builds/x64/bin/carbon-engine
```

---

## License

This project is licensed under the [GNU General Public License v3.0 (GPLv3)](LICENSE).

