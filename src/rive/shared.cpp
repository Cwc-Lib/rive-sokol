#include <float.h>
#include <string.h>

#include <file.hpp>
#include <artboard.hpp>

#include <jc/array.h>
#include "rive/shared.h"

namespace rive
{
    static RenderMode      g_RiveRenderMode      = MODE_STENCIL_TO_COVER;
    static RequestBufferCb g_RiveRequestBufferCb = 0;
    static DestroyBufferCb g_RiveDestroyBufferCb = 0;
    static float           g_RiveContourQuality  = 0.0f;

    static void getColorArrayFromUint(unsigned int colorIn, float* rgbaOut)
    {
        rgbaOut[0] = (float)((0x00ff0000 & colorIn) >> 16) / 255.0f;
        rgbaOut[1] = (float)((0x0000ff00 & colorIn) >> 8)  / 255.0f;
        rgbaOut[2] = (float)((0x000000ff & colorIn) >> 0)  / 255.0f;
        rgbaOut[3] = (float)((0xff000000 & colorIn) >> 24) / 255.0f;
    }

    static bool tooFar(const Vec2D& a, const Vec2D& b, float distTooFar)
    {
        return fmax(fabs(a[0] - b[0]), fabs(a[1] - b[1])) > distTooFar;
    }

    static void computeHull(const Vec2D& from,
                            const Vec2D& fromOut,
                            const Vec2D& toIn,
                            const Vec2D& to,
                            float t,
                            Vec2D* hull)
    {
        Vec2D::lerp(hull[0], from, fromOut, t);
        Vec2D::lerp(hull[1], fromOut, toIn, t);
        Vec2D::lerp(hull[2], toIn, to, t);

        Vec2D::lerp(hull[3], hull[0], hull[1], t);
        Vec2D::lerp(hull[4], hull[1], hull[2], t);

        Vec2D::lerp(hull[5], hull[3], hull[4], t);
    }

    static bool shouldSplitCubic(const Vec2D& from,
                                 const Vec2D& fromOut,
                                 const Vec2D& toIn,
                                 const Vec2D& to,
                                 float distTooFar)
    {
        Vec2D oneThird, twoThird;
        Vec2D::lerp(oneThird, from, to, 1.0f / 3.0f);
        Vec2D::lerp(twoThird, from, to, 2.0f / 3.0f);
        return tooFar(fromOut, oneThird, distTooFar) || tooFar(toIn, twoThird, distTooFar);
    }

    static float cubicAt(float t, float a, float b, float c, float d)
    {
        float ti = 1.0f - t;
        float value =
            ti * ti * ti * a +
            3.0f * ti * ti * t * b +
            3.0f * ti * t * t * c +
            t * t * t * d;
        return value;
    }

    void segmentCubic(const Vec2D& from,
                      const Vec2D& fromOut,
                      const Vec2D& toIn,
                      const Vec2D& to,
                      float t1,
                      float t2,
                      float minSegmentLength,
                      float distTooFar,
                      float* vertices,
                      uint32_t& verticesCount,
                      PathLimits* pathLimits)
    {

        if (shouldSplitCubic(from, fromOut, toIn, to, distTooFar))
        {
            float halfT = (t1 + t2) / 2.0f;

            Vec2D hull[6];
            computeHull(from, fromOut, toIn, to, 0.5f, hull);

            segmentCubic(from,
                         hull[0],
                         hull[3],
                         hull[5],
                         t1,
                         halfT,
                         minSegmentLength,
                         distTooFar,
                         vertices,
                         verticesCount,
                         pathLimits);
            segmentCubic(hull[5],
                         hull[4],
                         hull[2],
                         to,
                         halfT,
                         t2,
                         minSegmentLength,
                         distTooFar,
                         vertices,
                         verticesCount,
                         pathLimits);
        }
        else
        {
            float length = Vec2D::distance(from, to);
            if (length > minSegmentLength)
            {
                float x              = cubicAt(t2, from[0], fromOut[0], toIn[0], to[0]);
                float y              = cubicAt(t2, from[1], fromOut[1], toIn[1], to[1]);
                vertices[verticesCount * 2    ] = x;
                vertices[verticesCount * 2 + 1] = y;
                verticesCount++;

                if (pathLimits)
                {
                    pathLimits->m_MinX = fmin(pathLimits->m_MinX, x);
                    pathLimits->m_MinY = fmin(pathLimits->m_MinY, y);
                    pathLimits->m_MaxX = fmax(pathLimits->m_MaxX, x);
                    pathLimits->m_MaxY = fmax(pathLimits->m_MaxY, y);
                }
            }
        }
    }

    SharedRenderPaint::SharedRenderPaint()
    : m_Builder(0)
    , m_Data({})
    {}

    void SharedRenderPaint::color(unsigned int value)
    {
        m_Data = {
            .m_FillType = FILL_TYPE_SOLID,
            .m_StopCount = 1
        };

        getColorArrayFromUint(value, &m_Data.m_Colors[0]);

        m_IsVisible = m_Data.m_Colors[3] > 0.0f;
    }

    void SharedRenderPaint::linearGradient(float sx, float sy, float ex, float ey)
    {
        m_Builder                 = new SharedRenderPaintBuilder();
        m_Builder->m_GradientType = FILL_TYPE_LINEAR;
        m_Builder->m_StartX       = sx;
        m_Builder->m_StartY       = sy;
        m_Builder->m_EndX         = ex;
        m_Builder->m_EndY         = ey;
    }

    void SharedRenderPaint::radialGradient(float sx, float sy, float ex, float ey)
    {
        m_Builder                 = new SharedRenderPaintBuilder();
        m_Builder->m_GradientType = FILL_TYPE_RADIAL;
        m_Builder->m_StartX       = sx;
        m_Builder->m_StartY       = sy;
        m_Builder->m_EndX         = ex;
        m_Builder->m_EndY         = ey;
    }

    void SharedRenderPaint::addStop(unsigned int color, float stop)
    {
        if (m_Builder->m_Stops.Size() == m_Builder->m_Stops.Capacity())
        {
            m_Builder->m_Stops.SetCapacity(m_Builder->m_Stops.Size() + 1);
        }

        m_Builder->m_Stops.Push({
            .m_Color = color,
            .m_Stop  = stop,
        });
    }

    void SharedRenderPaint::completeGradient()
    {
        m_Data             = {};
        m_Data.m_FillType  = m_Builder->m_GradientType;
        m_Data.m_StopCount = m_Builder->m_Stops.Size();

        m_Data.m_GradientLimits[0] = m_Builder->m_StartX;
        m_Data.m_GradientLimits[1] = m_Builder->m_StartY;
        m_Data.m_GradientLimits[2] = m_Builder->m_EndX;
        m_Data.m_GradientLimits[3] = m_Builder->m_EndY;

        m_IsVisible = false;
        assert(m_Data.m_StopCount < SharedRenderPaintData::MAX_STOPS);
        for (int i = 0; i < m_Builder->m_Stops.Size(); ++i)
        {
            const GradientStop& stop = m_Builder->m_Stops[i];
            getColorArrayFromUint(stop.m_Color, &m_Data.m_Colors[i * 4]);
            m_Data.m_Stops[i] = stop.m_Stop;

            if (m_Data.m_Colors[i*4 + 3] > 0.0f)
            {
                m_IsVisible = true;
            }
        }

        m_Builder->m_Stops.SetSize(0);
        m_Builder->m_Stops.SetCapacity(0);
        delete m_Builder;
    }

    /* Shared RenderPaint */
    void SharedRenderPath::addRenderPath(RenderPath* path, const Mat2D& transform)
    {
        PathDescriptor desc = {path, transform};

        if (m_Paths.Full())
        {
            m_Paths.SetCapacity(m_Paths.Capacity() + 1);
        }

        m_Paths.Push(desc);
    }

    void SharedRenderPath::fillRule(FillRule value)
    {
        m_FillRule = value;
    }

    void SharedRenderPath::moveTo(float x, float y)
    {
        if (m_PathCommands.Full())
        {
            m_PathCommands.SetCapacity(m_PathCommands.Capacity() + 1);
        }

    #if PRINT_COMMANDS
        printf("moveTo (%f,%f)\n", x, y);
    #endif

        m_PathCommands.Push({
            .m_Command = TYPE_MOVE,
            .m_X       = x,
            .m_Y       = y
        });
    }

    void SharedRenderPath::lineTo(float x, float y)
    {
        if (m_PathCommands.Full())
        {
            m_PathCommands.SetCapacity(m_PathCommands.Capacity() + 1);
        }

    #if PRINT_COMMANDS
        printf("lineTo (%f,%f)\n", x, y);
    #endif

        m_PathCommands.Push({
            .m_Command = TYPE_LINE,
            .m_X       = x,
            .m_Y       = y
        });
    }

    void SharedRenderPath::cubicTo(float ox, float oy, float ix, float iy, float x, float y)
    {
        if (m_PathCommands.Full())
        {
            m_PathCommands.SetCapacity(m_PathCommands.Capacity() + 1);
        }
    #if PRINT_COMMANDS
        printf("cubicTo (%f,%f,%f,%f,%f,%f)\n", ox, oy, ix, iy, x, y);
    #endif

        m_PathCommands.Push({
            .m_Command = TYPE_CUBIC,
            .m_X       = x,
            .m_Y       = y,
            .m_OX      = ox,
            .m_OY      = oy,
            .m_IX      = ix,
            .m_IY      = iy,
        });
    }

    void SharedRenderPath::close()
    {
        if (m_PathCommands.Full())
        {
            m_PathCommands.SetCapacity(m_PathCommands.Capacity() + 1);
        }

    #if PRINT_COMMANDS
        printf("close\n");
    #endif

        m_PathCommands.Push({
            .m_Command = TYPE_CLOSE
        });
    }

    SharedRenderPath::SharedRenderPath()
    : m_ContourVertexCount(0)
    , m_IsDirty(true)
    , m_IsShapeDirty(true)
    {
        memset(m_ContourVertexData, 0, sizeof(m_ContourVertexData));
    }

    void SharedRenderPath::reset()
    {
        m_Paths.SetCapacity(0);
        m_Paths.SetSize(0);
        m_PathCommands.SetCapacity(0);
        m_PathCommands.SetSize(0);
        m_IsDirty = true;
        m_IsShapeDirty = true;
    }

    bool SharedRenderPath::isShapeDirty()
    {
        bool dirty = m_IsShapeDirty;
        if (dirty)
        {
            return true;
        }

        if (m_Paths.Size() > 0)
        {
            for (int i = 0; i < (int)m_Paths.Size(); ++i)
            {
                SharedRenderPath* sharedPath = (SharedRenderPath*) m_Paths[i].m_Path;
                if (sharedPath->isShapeDirty())
                {
                    dirty = true;
                    break;
                }
            }
        }

        return dirty;
    }

    /* Shared Renderer */
    void SharedRenderer::clipPath(RenderPath* path)
    {
        if (m_ClipPaths.Full())
        {
            m_ClipPaths.SetCapacity(m_ClipPaths.Capacity() + 1);
        }

        m_ClipPaths.Push({.m_Path = path, .m_Transform = m_Transform});
    }

    void SharedRenderer::startFrame()
    {
        m_AppliedClips.SetSize(0);
        m_DrawCalls.SetSize(0);
    }

    void SharedRenderer::transform(const Mat2D& transform)
    {
        Mat2D::multiply(m_Transform, m_Transform, transform);
    }

    void SharedRenderer::save()
    {
        StackEntry entry = {};
        entry.m_Transform = Mat2D(m_Transform);

        assert(m_ClipPaths.Size() < STACK_ENTRY_MAX_CLIP_PATHS);

        entry.m_ClipPathsCount = m_ClipPaths.Size();
        memcpy(entry.m_ClipPaths, m_ClipPaths.Begin(), m_ClipPaths.Size() * sizeof(PathDescriptor));

        if (m_ClipPathStack.Full())
        {
            m_ClipPathStack.SetCapacity(m_ClipPathStack.Capacity() + 1);
        }

        m_ClipPathStack.Push(entry);
    }

    void SharedRenderer::restore()
    {
        const StackEntry last = m_ClipPathStack.Pop();
        m_Transform = last.m_Transform;
        m_ClipPaths.SetSize(0);
        m_ClipPaths.SetCapacity(last.m_ClipPathsCount);

        for (int i = 0; i < last.m_ClipPathsCount; ++i)
        {
            m_ClipPaths.Push(last.m_ClipPaths[i]);
        }
    }

    void SharedRenderer::pushDrawCall(const PathDrawCall& dc)
    {
        if (m_DrawCalls.Full())
        {
            m_DrawCalls.SetCapacity(m_DrawCalls.Capacity() + 1);
        }
        m_DrawCalls.Push(dc);
    }

    /* Misc functions */
    float getContourError()
    {
        const float maxContourError = 5.0f;
        const float minContourError = 0.5f;
        return minContourError * g_RiveContourQuality + maxContourError * (1.0f - g_RiveContourQuality);
    }

    void setRenderMode(RenderMode mode)
    {
        g_RiveRenderMode = mode;
    }

    void setBufferCallbacks(RequestBufferCb rcb, DestroyBufferCb dcb)
    {
        g_RiveRequestBufferCb = rcb;
        g_RiveDestroyBufferCb = dcb;
    }

    void setContourQuality(float quality)
    {
        g_RiveContourQuality = quality;
    }

    HBuffer requestBuffer(HBuffer buffer, BufferType bufferType, void* data, unsigned int dataSize)
    {
        assert(g_RiveRequestBufferCb);
        return g_RiveRequestBufferCb(buffer, bufferType, data, dataSize);
    }

    void destroyBuffer(HBuffer buffer)
    {
        assert(g_RiveDestroyBufferCb);
        g_RiveDestroyBufferCb(buffer);
    }

    RenderMode getRenderMode()
    {
        return g_RiveRenderMode;
    }

    RenderPaint* makeRenderPaint()
    {
        return new SharedRenderPaint;
    }

    RenderPath* makeRenderPath()
    {
        switch(g_RiveRenderMode)
        {
            case MODE_TESSELLATION:     return new TessellationRenderPath;
            case MODE_STENCIL_TO_COVER: return new StencilToCoverRenderPath;
            default:break;
        }
        return 0;
    }

    Renderer* makeRenderer()
    {
        switch(g_RiveRenderMode)
        {
            case MODE_TESSELLATION:     return new TessellationRenderer;
            case MODE_STENCIL_TO_COVER: return new StencilToCoverRenderer;
            default:break;
        }
        return 0;
    }

    Artboard* loadArtboardFromData(uint8_t* data, size_t dataLength)
    {
        File* file          = 0;
        BinaryReader reader = BinaryReader(data, dataLength);
        ImportResult result = File::import(reader, &file);
        if (result != ImportResult::success)
        {
            return 0;
        }

        return file->artboard();
    }
}