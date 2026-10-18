# Carbon Engine Server - REST API Specification

Detailed technical reference for the `carbon-engine` HTTP REST API, including endpoint specifications, header parameters, JSON request payloads, response schemas, and `curl` usage examples.

---

## Global Conventions

- **Transport**: Standard HTTP / HTTPS
- **Data Format**: `application/json` (UTF-8)
- **CORS Support**: All endpoints return `Access-Control-Allow-Origin: *` and accept `Content-Type, Authorization, X-Gemini-API-Key`
- **Error Responses**: All client and server errors return a standard JSON object:
  ```json
  {
    "error": "Error description message",
    "status_code": 400
  }
  ```

---

## Authentication & Bring Your Own Key (BYOK)

To avoid server-managed token charges, clients provide their own Gemini API credentials using either of the following mechanisms:

1. **HTTP Header (Recommended)**:
   ```http
   X-Gemini-API-Key: AIzaSy...
   ```
   *(or `Authorization: Bearer AIzaSy...`)*
2. **Request Body Parameter**:
   Include `"api_key": "AIzaSy..."` directly in the payload for `/api/ingest`, `/api/extraction/start`, `/api/config/test`, or `/api/models`.
3. Keys supplied during `POST /api/ingest` or `POST /api/extraction/start` are attached directly to that job in the database and utilized for all subsequent vector embedding generation and statutory report extraction.

---

## Table of Contents

- [System & Health](#system--health)
  - [GET /api/system](#get-apisystem)
  - [GET /logo.png](#get-logopng)
- [Configuration & Models](#configuration--models)
  - [GET /api/mode](#get-apimode)
  - [POST /api/mode](#post-apimode)
  - [POST /api/config/test](#post-apiconfigtest)
  - [GET / POST /api/models](#get--post-apimodels)
- [Document Ingestion & Management](#document-ingestion--management)
  - [POST /api/upload](#post-apiupload)
  - [POST /api/ingest](#post-apiingest)
  - [GET /api/jobs](#get-apijobs)
  - [POST /api/jobs/cancel](#post-apijobscancel)
  - [POST /api/jobs/rate_limit](#post-apijobsrate_limit)
  - [GET /api/documents](#get-apidocuments)
- [Statutory BRSR Extraction](#statutory-brsr-extraction)
  - [POST /api/extraction/start](#post-apiextractionstart)
  - [GET /api/extraction/status](#get-apiextractionstatus)
  - [POST /api/extraction/cancel](#post-apiextractioncancel)
  - [POST /api/extraction/rate_limit](#post-apiextractionrate_limit)
- [Audit Review & Section Editing](#audit-review--section-editing)
  - [GET /api/extraction/sections](#get-apiextractionsections)
  - [PUT /api/extraction/sections](#put-apiextractionsections)
  - [POST /api/extraction/accept](#post-apiextractionaccept)
- [Reports & Exports](#reports--exports)
  - [POST /api/report/export](#post-apireportexport)
  - [GET /api/reports](#get-apireports)
  - [DELETE /api/reports](#delete-apireports)
- [BRSR Schema Catalog](#brsr-schema-catalog)
  - [GET /api/schema/brsr](#get-apischemabrsr)

---

## System & Health

### `GET /api/system`
Returns real-time engine health, memory usage, database statistics, queue depth, and cloud status.

- **Request**: None
- **Sample Request**:
  ```bash
  curl http://localhost:8080/api/system
  ```
- **Response `200 OK`**:
  ```json
  {
    "active_engine_mode": "GCLOUD",
    "active_model": "gemini-2.5-flash",
    "api_key_configured": false,
    "cloud_enabled": true,
    "cloud_model": "gemini-2.5-flash",
    "cloud_pacing_status": "NORMAL",
    "cpu_pct_process": 0.5,
    "cpu_pct_system": 1.2,
    "db_status": "14.2 MB (320 chunks)",
    "engine_status": "Idle",
    "gpu_layers_offloaded": 0,
    "gpu_offload_mode": "CPU Only",
    "gpu_pct": null,
    "gpu_present": false,
    "job_queue_depth": 0,
    "memory_total_mb_system": 8192,
    "memory_used_mb_process": 56,
    "memory_used_mb_system": 1024,
    "tokens_per_sec": 0,
    "vram_total_mb": 0,
    "vram_used_mb": 0
  }
  ```

### `GET /logo.png`
Serves the embedded brand asset image.

- **Response `200 OK`**: Binary image (`image/png`)

---

## Configuration & Models

### `GET /api/mode`
Retrieves current processing mode, configured models, and pacing status.

- **Request**: None
- **Response `200 OK`**:
  ```json
  {
    "mode": "GCLOUD",
    "cloud_enabled": true,
    "api_key_configured": false,
    "api_key": "",
    "cloud_model": "gemini-2.5-flash",
    "cloud_embedding_model": "text-embedding-004",
    "available_models": ["gemini-2.5-flash", "gemini-2.0-flash", "gemini-1.5-flash"],
    "available_embedding_models": ["text-embedding-004"],
    "local_model": "gemma-2-2b-it",
    "pacing_status": "NORMAL"
  }
  ```

### `POST /api/mode`
Dynamically updates server processing mode and active models.

- **Request Body**:
  ```json
  {
    "cloud_enabled": true,
    "cloud_model": "gemini-2.5-flash",
    "cloud_embedding_model": "text-embedding-004"
  }
  ```
- **Response `200 OK`**:
  ```json
  {
    "status": "UPDATED",
    "mode": "GCLOUD",
    "cloud_enabled": true,
    "cloud_model": "gemini-2.5-flash",
    "cloud_embedding_model": "text-embedding-004"
  }
  ```

### `POST /api/config/test`
Validates Gemini API connectivity and measures live network latency.

- **Request Headers** *(Optional)*: `X-Gemini-API-Key: AIzaSy...`
- **Request Body** *(Optional overrides)*:
  ```json
  {
    "api_key": "AIzaSy...",
    "cloud_model": "gemini-2.5-flash",
    "cloud_embedding_model": "text-embedding-004"
  }
  ```
- **Response `200 OK`**:
  ```json
  {
    "success": true,
    "inference_success": true,
    "model": "gemini-2.5-flash",
    "latency_ms": 382,
    "embedding_success": true,
    "embedding_model": "text-embedding-004",
    "embedding_latency_ms": 145,
    "embedding_dim": 768,
    "available_models": ["gemini-2.5-flash", "gemini-2.0-flash"]
  }
  ```

### `GET /api/models` / `POST /api/models`
Queries Google AI Studio to list available Gemini generation and embedding models.

- **Request Headers** *(Optional)*: `X-Gemini-API-Key: AIzaSy...`
- **Request Body** *(Optional)*: `{"api_key": "..."}`
- **Response `200 OK`**:
  ```json
  {
    "available_models": ["gemini-2.5-flash", "gemini-2.0-flash", "gemini-1.5-flash"],
    "available_embedding_models": ["text-embedding-004"]
  }
  ```

---

## Document Ingestion & Management

### `POST /api/upload`
Uploads files encoded in Base64 directly into the container scratch directory (`CARBON_ENGINE_UPLOAD_DIR`).

- **Request Body**:
  ```json
  {
    "files": [
      {
        "filename": "TATA_STEEL_BRSR_2026.pdf",
        "data": "JVBERi0xLjQKJ..."
      }
    ]
  }
  ```
- **Response `200 OK`**:
  ```json
  {
    "status": "SUCCESS",
    "uploaded_files": [
      {
        "filename": "TATA_STEEL_BRSR_2026.pdf",
        "file_path": "/app/data/uploads/1740000000000_TATA_STEEL_BRSR_2026.pdf",
        "size_bytes": 4829104
      }
    ]
  }
  ```

### `POST /api/ingest`
Enqueues document binders for ingestion, layout parsing, OCR, and vector indexing.

- **Request Headers** *(Optional)*: `X-Gemini-API-Key: AIzaSy...`
- **Option A: Inline File Upload**:
  ```json
  {
    "files": [
      {
        "filename": "annual_report.pdf",
        "data": "JVBERi0xLjQKJ..."
      }
    ],
    "api_key": "AIzaSy..."
  }
  ```
- **Option B: Existing File Paths**:
  ```json
  {
    "file_paths": [
      "/app/data/uploads/annual_report.pdf"
    ],
    "api_key": "AIzaSy..."
  }
  ```
- **Response `200 OK`**:
  ```json
  {
    "status": "QUEUED",
    "job_id": 1
  }
  ```

### `GET /api/jobs`
Queries the status of an ingestion job.

- **Query Parameters**: `job_id` (integer, required)
- **Sample Request**:
  ```bash
  curl "http://localhost:8080/api/jobs?job_id=1"
  ```
- **Response `200 OK`**:
  ```json
  {
    "job_id": 1,
    "status": "COMPLETED",
    "extraction_status": "NOT_STARTED",
    "report_locked": false,
    "documents": [
      {
        "filename": "annual_report.pdf",
        "pages": 142,
        "chunks": 320,
        "format": "PDF"
      }
    ],
    "rate_limited": false
  }
  ```

### `POST /api/jobs/cancel`
Cancels an ongoing document ingestion job.

- **Request Body**: `{"job_id": 1}`
- **Response `200 OK`**:
  ```json
  {
    "status": "CANCELLED",
    "job_id": 1,
    "message": "Ingestion halted. All in-flight API calls cancelled."
  }
  ```

### `POST /api/jobs/rate_limit`
Resolves a 429 rate limit condition encountered during vector embedding generation.

- **Request Body**:
  ```json
  {
    "job_id": 1,
    "action": "switch_model",
    "new_model": "text-embedding-004"
  }
  ```
  *Allowed actions*: `retry`, `switch_model`, `switch_local`, `cancel`.

### `GET /api/documents`
Lists documents associated with an ingestion job.

- **Query Parameters**: `job_id` (integer, required)
- **Response `200 OK`**:
  ```json
  {
    "job_id": 1,
    "documents": [
      {
        "filename": "annual_report.pdf",
        "pages": 142,
        "chunks": 320
      }
    ]
  }
  ```

---

## Statutory BRSR Extraction

### `POST /api/extraction/start`
Triggers statutory compliance metric extraction. Ingestion must be `COMPLETED`.

- **Request Headers** *(Optional)*: `X-Gemini-API-Key: AIzaSy...`
- **Request Body**:
  ```json
  {
    "job_id": 1,
    "api_key": "AIzaSy..."
  }
  ```
  *(api_key is optional if provided during ingestion or via header)*
- **Response `200 OK`**:
  ```json
  {
    "status": "EXTRACTING",
    "job_id": 1
  }
  ```

### `GET /api/extraction/status`
Live extraction progress, active section, tokens consumed, and gap detection.

- **Query Parameters**: `job_id` (integer, required)
- **Response `200 OK`**:
  ```json
  {
    "job_id": 1,
    "status": "EXTRACTING",
    "engine_mode": "GCLOUD",
    "active_model": "gemini-2.5-flash",
    "cloud_pacing_status": "NORMAL",
    "current_step": 3,
    "total_steps": 11,
    "progress_pct": 27.3,
    "current_group": "Principle 1: Ethics, Transparency & Accountability",
    "current_group_id": "P1",
    "last_latency_ms": 1280,
    "last_prompt_tokens": 4210,
    "last_candidate_tokens": 650,
    "missing_metrics": [],
    "rate_limited": false
  }
  ```

### `POST /api/extraction/cancel`
Halts an ongoing statutory extraction pipeline.

- **Request Body**: `{"job_id": 1}`
- **Response `200 OK`**:
  ```json
  {
    "status": "CANCELLED",
    "job_id": 1,
    "message": "Extraction halted. All in-flight API calls cancelled."
  }
  ```

### `POST /api/extraction/rate_limit`
Resolves an LLM inference rate limit.

- **Request Body**:
  ```json
  {
    "job_id": 1,
    "action": "switch_model",
    "new_model": "gemini-1.5-flash"
  }
  ```
  *Allowed actions*: `retry`, `switch_model`, `switch_local`, `cancel`.

---

## Audit Review & Section Editing

### `GET /api/extraction/sections`
Fetches extracted section metrics, tabular arrays, provenance citations, and validation warnings.

- **Query Parameters**: `job_id` (integer, required)
- **Response `200 OK`**:
  ```json
  {
    "job_id": 1,
    "sections": [
      {
        "group_id": "GENERAL_DISCLOSURES",
        "group_label": "Section A: General Disclosures",
        "status": "DRAFT",
        "data": {
          "company_name": "Tata Steel Limited",
          "cin": "L27100MH1907PLC000260",
          "reporting_year": "2026-2027",
          "turnover_inr_cr": 243352.69
        },
        "warnings": []
      }
    ]
  }
  ```

### `PUT /api/extraction/sections`
Updates drafted metric values during audit review before locking.

- **Request Body**:
  ```json
  {
    "job_id": 1,
    "group_id": "GENERAL_DISCLOSURES",
    "data": {
      "company_name": "Tata Steel Limited",
      "reporting_year": "2026-2027"
    }
  }
  ```
- **Response `200 OK`**:
  ```json
  {
    "status": "UPDATED",
    "group_id": "GENERAL_DISCLOSURES"
  }
  ```

### `POST /api/extraction/accept`
Finalizes and locks all report sections against future edits.

- **Request Body**: `{"job_id": 1}`
- **Response `200 OK`**:
  ```json
  {
    "status": "ACCEPTED",
    "job_id": 1
  }
  ```

---

## Reports & Exports

### `POST /api/report/export`
Compiles disclosures into a self-contained, audit-ready HTML compliance report with Inja templating.

- **Request Body**:
  ```json
  {
    "job_id": 1,
    "draft": false
  }
  ```
- **Response `200 OK`**:
  ```json
  {
    "job_id": 1,
    "is_locked": true,
    "report": "<!DOCTYPE html><html>...</html>"
  }
  ```

### `GET /api/reports`
Lists historical compliance reports and their lock status.

- **Query Parameters** *(Optional)*: `limit`, `offset`
- **Response `200 OK`**:
  ```json
  {
    "reports": [
      {
        "job_id": 1,
        "company_name": "Tata Steel Limited",
        "created_at": "2026-10-18T10:00:00Z",
        "is_locked": true,
        "status": "ACCEPTED"
      }
    ],
    "total": 1
  }
  ```

### `DELETE /api/reports`
Permanently deletes a report, its SQLite rows, vector index, and cached facts.

- **Query Parameters**: `job_id` (integer, required)
- **Response `200 OK`**:
  ```json
  {
    "status": "DELETED",
    "job_id": 1
  }
  ```

---

## BRSR Schema Catalog

### `GET /api/schema/brsr`
Retrieves statutory BRSR taxonomy metadata, metric identifiers, and principle definitions.

- **Request**: None
- **Response `200 OK`**:
  ```json
  {
    "version": "BRSR-Core-2026",
    "groups": [
      {
        "id": "GENERAL_DISCLOSURES",
        "title": "General Disclosures"
      },
      {
        "id": "P1",
        "title": "Principle 1: Ethics, Transparency & Accountability"
      }
    ]
  }
  ```

