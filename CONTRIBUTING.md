# Contributing to Carbon Engine Server

Thank you for your interest in contributing to **Carbon Engine Server**! We welcome contributions from systems engineers, machine learning practitioners, and ESG/sustainability domain specialists.

This repository hosts the standalone C++20 headless REST API daemon and container image for cloud and serverless deployments (Google Cloud Run, AWS ECS, Kubernetes).

---

## 💡 Contribution Philosophy: Discuss Before Coding

To ensure your time and effort are well spent, **please open an issue to discuss your proposal before submitting a major Pull Request (PR)**.

- **New Features & Architectural Changes**: Open an issue detailing your use case, REST API contract, and implementation strategy. This allows maintainers and the community to align on design decisions and ensure consistency with the stateless container architecture.
- **API Changes**: Any modifications to HTTP endpoints, query parameters, or response payloads must be reflected in [API.md](API.md).
- **Small Fixes**: Minor bug fixes, documentation corrections, or small CMake/Docker tweaks do not require prior discussion and can be submitted directly as PRs.

---

## 🐛 Reporting Bugs

When filing a bug report, please include the following details:

### 1. Observed Behavior
- Exact error messages, HTTP status codes, or stack traces.
- Relevant container logs (`docker logs <container-id>` or `/app/data/logs/*.log`).
- Request payload and curl commands used to reproduce the failure.

### 2. Expected Behavior
- Describe what you expected to occur based on the API specification and statutory requirements.

### 3. Environment Profile
- **Host OS**: Ubuntu, Debian, Windows (WSL2), macOS.
- **Container Engine**: Docker version, Docker Compose, or Cloud Run configuration.
- **Hardware Profile**: CPU architecture (x86_64, aarch64), available system RAM.
- **Toolchain (if building locally)**:
  - Compiler: GCC 11+, Clang 14+, or MSVC 2022
  - Qt version: Qt 6.5+ (Qt HttpServer, Core, Network, Sql)
  - CMake version: CMake 3.24+

### 4. Steps to Reproduce
- Numbered sequence of steps from container startup to endpoint invocation.

---

## 🛠️ C++20 Standards & Best Practices

All server code in `src/` follows modern C++20 principles. Please adhere to the following standards:

### 1. Resource Management & RAII
- Rely on RAII (Resource Acquisition Is Initialization) for all resource lifetimes.
- Never use raw `new` and `delete`. Use `std::make_unique` or `std::make_shared`.
- Wrap third-party C handles (PDFium, Leptonica, Tesseract) in RAII wrappers or smart pointers with custom deleters.

### 2. Const-Correctness & Performance
- Mark member functions, variables, and parameters `const` wherever mutation is not required.
- Pass read-only strings and spans via `std::string_view`, `QByteArrayView`, or `std::span` to avoid heap allocations.

### 3. Asynchronous Execution & Concurrency
- `QHttpServer` routes must execute quickly without blocking the main event loop.
- Long-running tasks (document parsing, embedding generation, LLM extraction) must be offloaded to worker threads via `JobQueue` or `QThreadPool`.
- Protect shared mutable state using appropriate synchronization primitives (`std::mutex`, `std::lock_guard`, `std::atomic`).

### 4. Database Safety
- All SQLite queries must use parameterized SQL (`QSqlQuery::prepare` with named binds). Never concatenate raw strings into SQL statements.
- Database schema changes must be implemented as numbered migrations in `Database::runMigrations()`.

### 5. Memory Safety & Boundary Validation
- Validate all incoming JSON requests and query parameters before processing.
- Perform explicit boundary checks when parsing untrusted binary files, Base64 buffers, or PDF streams.

### 6. Bring Your Own Key (BYOK) Compliance
- Never commit hardcoded API keys.
- Do not log client-provided Gemini API keys to stdout, stderr, or log files.
- Keys must remain scoped strictly to the respective job/request.

---

## 🚀 Pull Request Workflow

1. **Fork & Branch**: Create a feature branch with a descriptive name:
   - `git checkout -b feat/add-batch-extraction`
   - `git checkout -b fix/sqlite-wal-checkpoint`
2. **Build & Test**: Ensure the Docker image compiles cleanly and passes the health check:
   ```bash
   docker build -t carbon-engine-server:test .
   docker run --rm -p 8080:8080 carbon-engine-server:test
   curl http://localhost:8080/api/system
   ```
3. **Commit Guidelines**: Write clear, imperative commit messages:
   - `feat: add retry mechanism for Gemini rate limit resolution`
   - `fix: handle empty base64 uploads gracefully in /api/upload`
   - `docs: update API.md with new extraction parameters`
4. **Documentation**: Update [README.md](README.md) and [API.md](API.md) whenever endpoint contracts change.

