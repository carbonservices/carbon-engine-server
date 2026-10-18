#include "DocumentProcessor.h"
#include <fpdf_text.h>
#include <fpdf_edit.h>
#include <QString>
#include <QtGlobal>
#include <algorithm>
#include <cctype>
#include <stdexcept>

constexpr int kNativeTextCharThreshold = 32;

bool NativeTextExtractor::canHandle(FPDF_TEXTPAGE textPage) const
{
    return FPDFText_CountChars(textPage) > kNativeTextCharThreshold;
}

std::string NativeTextExtractor::extract(FPDF_PAGE /*page*/, FPDF_TEXTPAGE textPage)
{
    int charCount = FPDFText_CountChars(textPage);
    std::vector<unsigned short> buffer(charCount + 1);
    FPDFText_GetText(textPage, 0, charCount, buffer.data());

    // FPDFText_GetText fills UTF-16LE code units; QString::fromUtf16 handles
    // the transcode to UTF-8 correctly instead of truncating each unit to a
    // single byte (which mangles any non-ASCII text).
    QString text = QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.data()), charCount);
    return text.toStdString();
}

OcrTextExtractor::OcrTextExtractor()
{
    _tess.Init(Q_NULLPTR, "eng", tesseract::OEM_LSTM_ONLY);
    _tess.SetPageSegMode(tesseract::PSM_AUTO);
}

OcrTextExtractor::~OcrTextExtractor()
{
    _tess.End();
}

bool OcrTextExtractor::canHandle(FPDF_TEXTPAGE textPage) const
{
    return FPDFText_CountChars(textPage) <= kNativeTextCharThreshold;
}

std::string OcrTextExtractor::extract(FPDF_PAGE page, FPDF_TEXTPAGE /*textPage*/)
{
    if (!page) return "";

    // 1. Fast PDF Object Check: If page contains 0 objects, it is mathematically blank
    if (FPDFPage_CountObjects(page) == 0) {
        return "";
    }

    constexpr float kOcrScale = 2.0f; // Render at ~144-150 DPI for accurate Tesseract recognition
    int width = static_cast<int>(FPDF_GetPageWidth(page) * kOcrScale);
    int height = static_cast<int>(FPDF_GetPageHeight(page) * kOcrScale);
    if (width <= 0 || height <= 0) return "";

    FPDF_BITMAP bitmap = FPDFBitmap_Create(width, height, 0);
    if (!bitmap) return "";

    FPDFBitmap_FillRect(bitmap, 0, 0, width, height, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, width, height, 0, 0);

    const void *buffer = FPDFBitmap_GetBuffer(bitmap);
    int stride = FPDFBitmap_GetStride(bitmap);

    // 2. Fast Ink Check: If >= 99.85% of sampled pixels are white, skip OCR
    const uint32_t *pixels = reinterpret_cast<const uint32_t *>(buffer);
    int totalPixels = width * height;
    int step = std::max(1, totalPixels / 1000);
    int nonWhite = 0;
    int samples = 0;
    for (int p = 0; p < totalPixels; p += step) {
        samples++;
        if ((pixels[p] & 0x00FFFFFF) != 0x00FFFFFF) {
            nonWhite++;
        }
    }
    if (samples > 0 && (static_cast<float>(nonWhite) / samples) < 0.0015f) {
        FPDFBitmap_Destroy(bitmap);
        return "";
    }

    _tess.SetImage(static_cast<const unsigned char *>(buffer), width, height, 4, stride);
    char *ocrText = _tess.GetUTF8Text();
    std::string result(ocrText ? ocrText : "");

    delete[] ocrText;
    FPDFBitmap_Destroy(bitmap);
    return result;
}

DocumentProcessor::DocumentProcessor()
{
    FPDF_InitLibrary();
    _extractors.push_back(std::make_unique<NativeTextExtractor>());
    _extractors.push_back(std::make_unique<OcrTextExtractor>());
}

DocumentProcessor::~DocumentProcessor()
{
    FPDF_DestroyLibrary();
}

std::vector<DocumentChunk> DocumentProcessor::processPdf(const std::string &pdfPath)
{
    std::vector<DocumentChunk> chunks;
    FPDF_DOCUMENT doc = FPDF_LoadDocument(pdfPath.c_str(), Q_NULLPTR);
    if (!doc)
        throw std::runtime_error("Failed to load PDF: " + pdfPath);

    int pageCount = FPDF_GetPageCount(doc);
    for (int i = 0; i < pageCount; ++i)
    {
        FPDF_PAGE page = FPDF_LoadPage(doc, i);
        if (!page)
            continue;

        FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
        if (!textPage)
        {
            FPDF_ClosePage(page);
            continue;
        }

        std::string extractedText;
        for (auto &extractor : _extractors)
        {
            if (extractor->canHandle(textPage))
            {
                extractedText = extractor->extract(page, textPage);
                break;
            }
        }

        if (!extractedText.empty())
        {
            constexpr size_t maxChars = 1500;
            constexpr size_t overlap = 300;
            size_t start = 0;

            while (start < extractedText.length())
            {
                size_t end = start + maxChars;
                if (end < extractedText.length())
                {
                    // Two-tier boundary split:
                    // 1. Prioritize a newline in the last 200 chars to keep table rows & paragraphs intact
                    size_t lookbackMin = (end > 200 + start) ? (end - 200) : start;
                    size_t lastNewline = extractedText.rfind('\n', end);
                    if (lastNewline != std::string::npos && lastNewline > lookbackMin)
                    {
                        end = lastNewline;
                    }
                    else
                    {
                        // 2. Fall back to any whitespace separator
                        size_t lastWhitespace = extractedText.find_last_of(" \n\r\t", end);
                        if (lastWhitespace != std::string::npos && lastWhitespace > start)
                        {
                            end = lastWhitespace;
                        }
                    }
                }
                else
                {
                    end = extractedText.length();
                }

                std::string rawChunk = extractedText.substr(start, end - start);
                size_t firstNonWs = rawChunk.find_first_not_of(" \t\n\r");
                if (firstNonWs != std::string::npos)
                {
                    size_t lastNonWs = rawChunk.find_last_not_of(" \t\n\r");
                    std::string cleanChunk = rawChunk.substr(firstNonWs, lastNonWs - firstNonWs + 1);
                    if (cleanChunk.length() >= 20)
                    {
                        chunks.push_back({i + 1, std::move(cleanChunk)});
                    }
                }

                if (end >= extractedText.length())
                {
                    break;
                }

                size_t nextStart = (end > overlap + start) ? (end - overlap) : end;
                while (nextStart < end && !std::isspace(static_cast<unsigned char>(extractedText[nextStart])))
                {
                    nextStart++;
                }
                while (nextStart < extractedText.length() && std::isspace(static_cast<unsigned char>(extractedText[nextStart])))
                {
                    nextStart++;
                }
                if (nextStart <= start)
                {
                    nextStart = end;
                }
                start = nextStart;
            }
        }

        FPDFText_ClosePage(textPage);
        FPDF_ClosePage(page);
    }

    FPDF_CloseDocument(doc);
    return chunks;
}