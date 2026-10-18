# Security Policy

## Overview

**Carbon Engine Server** is a headless, native C++20 REST daemon designed for corporate ESG compliance and statutory SEBI BRSR analytics. In enterprise ESG workflows, annual reports and sustainability disclosures frequently contain Unpublished Price Sensitive Information (UPSI). 

We take all security considerations seriously, including container security, API authentication isolation, native parser boundaries, and SQL injection prevention.

---

## Supported Versions

Security updates are actively maintained for the following versions:

| Version | Supported |
|---|---|
| 1.x (Current master) | :white_check_mark: |
| < 1.0 | :x: |

---

## Reporting a Security Vulnerability

If you discover a security vulnerability in Carbon Engine Server, please **do not open a public GitHub issue**. Publicly disclosing an issue puts production deployments and sensitive compliance data at risk.

### Private Reporting Channels

Please submit vulnerability reports through one of the following methods:
1. **GitHub Private Vulnerability Reporting**: Navigate to the **Security** tab of this repository and select **"Report a vulnerability"**.
2. **Email**: Send detailed security advisories directly to `carbon.esgreports@gmail.com`.

### What to Include in Your Report

To help our security team triage and resolve issues efficiently, please include:
- A clear description of the vulnerability and its potential impact.
- Step-by-step reproduction instructions or a minimal Proof of Concept (PoC).
- Environment details: Container runtime (Docker, Cloud Run, Kubernetes), OS, and compiler/library versions.
- Relevant log output or sanitized HTTP request/response traces.
- If you have an advisory remediation or patch suggestion, feel free to attach it.

### Response Timeline

- **Initial Acknowledgement**: Within 48 hours of receipt.
- **Triage & Reproducibility Assessment**: Within 5 business days.
- **Remediation & Coordinated Disclosure**: We will collaborate with you to verify the fix and coordinate a release timeline before public disclosure.

---

## Core Security Commitments & Architecture

### 1. Bring Your Own Key (BYOK) Isolation & Zero Key Leakage
- **No Server-Side Token Storage**: The server does not maintain server-wide LLM keys. Clients supply their own Gemini API credentials per request or per job (`X-Gemini-API-Key` or payload).
- **Zero Credential Exposure**: Client API keys are strictly excluded from all `GET` telemetry and status endpoints (`/api/system`, `/api/jobs`, `/api/reports`, `/api/mode`).
- **Log Masking**: API keys are never echoed to stdout/stderr or written to persistent log files.
- **Cascading Purge**: When a report or job is deleted (`DELETE /api/reports?job_id=...`), the database row is deleted with cascading cleanup, permanently removing the associated key.

### 2. Container Hardening & Non-Root Execution
- The production Docker image enforces non-root execution under service user `carbon` (UID `10001`, GID `10001`).
- Multi-stage builds strip compilers, headers, and build dependencies from the final runtime container image.
- Volumes are restricted to `/app/data` for persistent SQLite storage and uploads.

### 3. Native Buffer & Parser Safety
Untrusted documents (PDF files and Base64 uploads) are parsed using native C++ libraries (PDFium, Tesseract OCR, Leptonica):
- Memory allocations are managed strictly through RAII smart pointers and custom deleters.
- Image buffer dimensions and page bounds are checked prior to rasterization to guard against memory corruption or integer overflow vulnerabilities.

### 4. Database Integrity & Injection Prevention
- Relational data is stored in SQLite 3 (WAL mode).
- All queries strictly utilize parameterized SQL statements (`QSqlQuery::prepare` with named binds), eliminating SQL injection vectors.

