#pragma once

#include <QString>

namespace JobStatus {
inline constexpr const char* Pending = "PENDING";
inline constexpr const char* Processing = "PROCESSING";
inline constexpr const char* Completed = "COMPLETED";
inline constexpr const char* Failed = "FAILED";
inline constexpr const char* Cancelled = "CANCELLED";
}

namespace ExtractionStatus {
inline constexpr const char* NotStarted = "NOT_STARTED";
inline constexpr const char* Extracting = "EXTRACTING";
inline constexpr const char* ReadyForReview = "READY_FOR_REVIEW";
inline constexpr const char* Completed = "COMPLETED";
inline constexpr const char* Accepted = "ACCEPTED";
inline constexpr const char* Failed = "FAILED";
inline constexpr const char* Cancelled = "CANCELLED";
}

namespace ReportSectionStatus {
inline constexpr const char* Draft = "DRAFT";
inline constexpr const char* Updated = "UPDATED";
inline constexpr const char* Failed = "FAILED";
}

namespace EngineMode {
inline constexpr const char* Offline = "OFFLINE";
inline constexpr const char* GCloud = "GCLOUD";
}

namespace Models {
inline constexpr const char* LocalGemmaName = "gemma-2-2b-it";
inline constexpr const char* LocalGemmaFile = "models/gemma-2-2b-it-q4_k_m.gguf";
inline constexpr const char* LocalBgeEmbeddingFile = "models/bge-small-en-v1.5-f16.gguf";
inline constexpr const char* CloudDefaultModel = "gemini-3.8-flash";
inline constexpr const char* CloudDefaultEmbeddingModel = "text-embedding-004";
}

namespace ApiRoutes {
inline constexpr const char* Ingest = "/api/ingest";
inline constexpr const char* Jobs = "/api/jobs";
inline constexpr const char* JobCancel = "/api/jobs/cancel";
inline constexpr const char* System = "/api/system";
inline constexpr const char* Mode = "/api/mode";
inline constexpr const char* ConfigTest = "/api/config/test";
inline constexpr const char* ExtractionStart = "/api/extraction/start";
inline constexpr const char* ExtractionStatus = "/api/extraction/status";
inline constexpr const char* ExtractionCancel = "/api/extraction/cancel";
inline constexpr const char* ExtractionSections = "/api/extraction/sections";
inline constexpr const char* ExtractionAccept = "/api/extraction/accept";
inline constexpr const char* ReportExport = "/api/report/export";
inline constexpr const char* Reports = "/api/reports";
inline constexpr const char* Documents = "/api/documents";
inline constexpr const char* Models = "/api/models";
inline constexpr const char* SchemaBrsr = "/api/schema/brsr";
inline constexpr const char* JobResolveRateLimit = "/api/jobs/resolve-rate-limit";
inline constexpr const char* ExtractionResolveRateLimit = "/api/extraction/resolve-rate-limit";
}

namespace ServerDefaults {
inline constexpr quint16 DefaultPort = 8080;
inline constexpr const char* DefaultDatabaseName = "ce-database";
inline constexpr int DefaultEmbeddingDim = 384;
}
