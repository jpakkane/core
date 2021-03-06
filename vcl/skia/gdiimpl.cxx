/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <skia/gdiimpl.hxx>

#include <salgdi.hxx>
#include <skia/salbmp.hxx>
#include <vcl/idle.hxx>
#include <vcl/svapp.hxx>
#include <vcl/lazydelete.hxx>
#include <vcl/skia/SkiaHelper.hxx>
#include <skia/utils.hxx>

#include <SkCanvas.h>
#include <SkPath.h>
#include <SkRegion.h>
#include <SkDashPathEffect.h>
#include <GrBackendSurface.h>

#include <basegfx/polygon/b2dpolygontools.hxx>

namespace
{
// Create Skia Path from B2DPolygon
// Note that polygons generally have the complication that when used
// for area (fill) operations they usually miss the right-most and
// bottom-most line of pixels of the bounding rectangle (see
// https://lists.freedesktop.org/archives/libreoffice/2019-November/083709.html).
// So be careful with rectangle->polygon conversions (generally avoid them).
void addPolygonToPath(const basegfx::B2DPolygon& rPolygon, SkPath& rPath)
{
    const sal_uInt32 nPointCount(rPolygon.count());

    if (nPointCount <= 1)
        return;

    const bool bClosePath(rPolygon.isClosed());
    const bool bHasCurves(rPolygon.areControlPointsUsed());

    bool bFirst = true;

    sal_uInt32 nCurrentIndex = 0;
    sal_uInt32 nPreviousIndex = nPointCount - 1;

    basegfx::B2DPoint aCurrentPoint;
    basegfx::B2DPoint aPreviousPoint;

    for (sal_uInt32 nIndex = 0; nIndex <= nPointCount; nIndex++)
    {
        if (nIndex == nPointCount && !bClosePath)
            continue;

        // Make sure we loop the last point to first point
        nCurrentIndex = nIndex % nPointCount;
        aCurrentPoint = rPolygon.getB2DPoint(nCurrentIndex);

        if (bFirst)
        {
            rPath.moveTo(aCurrentPoint.getX(), aCurrentPoint.getY());
            bFirst = false;
        }
        else if (!bHasCurves)
        {
            rPath.lineTo(aCurrentPoint.getX(), aCurrentPoint.getY());
        }
        else
        {
            basegfx::B2DPoint aPreviousControlPoint = rPolygon.getNextControlPoint(nPreviousIndex);
            basegfx::B2DPoint aCurrentControlPoint = rPolygon.getPrevControlPoint(nCurrentIndex);

            if (aPreviousControlPoint.equal(aPreviousPoint))
            {
                aPreviousControlPoint
                    = aPreviousPoint + ((aPreviousControlPoint - aCurrentPoint) * 0.0005);
            }

            if (aCurrentControlPoint.equal(aCurrentPoint))
            {
                aCurrentControlPoint
                    = aCurrentPoint + ((aCurrentControlPoint - aPreviousPoint) * 0.0005);
            }
            rPath.cubicTo(aPreviousControlPoint.getX(), aPreviousControlPoint.getY(),
                          aCurrentControlPoint.getX(), aCurrentControlPoint.getY(),
                          aCurrentPoint.getX(), aCurrentPoint.getY());
        }
        aPreviousPoint = aCurrentPoint;
        nPreviousIndex = nCurrentIndex;
    }
    if (bClosePath)
    {
        rPath.close();
    }
}

void addPolyPolygonToPath(const basegfx::B2DPolyPolygon& rPolyPolygon, SkPath& rPath)
{
    const sal_uInt32 nPolygonCount(rPolyPolygon.count());

    if (nPolygonCount == 0)
        return;

    for (const auto& rPolygon : rPolyPolygon)
    {
        addPolygonToPath(rPolygon, rPath);
    }
}

SkColor toSkColor(Color color)
{
    return SkColorSetARGB(255 - color.GetTransparency(), color.GetRed(), color.GetGreen(),
                          color.GetBlue());
}

SkColor toSkColorWithTransparency(Color aColor, double fTransparency)
{
    return SkColorSetA(toSkColor(aColor), 255 * (1.0 - fTransparency));
}

Color fromSkColor(SkColor color)
{
    return Color(255 - SkColorGetA(color), SkColorGetR(color), SkColorGetG(color),
                 SkColorGetB(color));
}

// returns true if the source or destination rectangles are invalid
bool checkInvalidSourceOrDestination(SalTwoRect const& rPosAry)
{
    return rPosAry.mnSrcWidth <= 0 || rPosAry.mnSrcHeight <= 0 || rPosAry.mnDestWidth <= 0
           || rPosAry.mnDestHeight <= 0;
}

} // end anonymous namespace

// Class that triggers flushing the backing buffer when idle.
class SkiaFlushIdle : public Idle
{
    SkiaSalGraphicsImpl* mpGraphics;

public:
    explicit SkiaFlushIdle(SkiaSalGraphicsImpl* pGraphics)
        : Idle("skia idle swap")
        , mpGraphics(pGraphics)
    {
        // We don't want to be swapping before we've painted.
        SetPriority(TaskPriority::POST_PAINT);
    }

    virtual void Invoke() override
    {
        mpGraphics->performFlush();
        Stop();
        SetPriority(TaskPriority::HIGHEST);
    }
};

SkiaSalGraphicsImpl::SkiaSalGraphicsImpl(SalGraphics& rParent, SalGeometryProvider* pProvider)
    : mParent(rParent)
    , mProvider(pProvider)
    , mIsGPU(false)
    , mLineColor(SALCOLOR_NONE)
    , mFillColor(SALCOLOR_NONE)
    , mXorMode(false)
    , mFlush(new SkiaFlushIdle(this))
    , mPendingPixelsToFlush(0)
{
}

SkiaSalGraphicsImpl::~SkiaSalGraphicsImpl()
{
    assert(!mSurface);
    assert(!mWindowContext);
}

void SkiaSalGraphicsImpl::Init() {}

void SkiaSalGraphicsImpl::recreateSurface()
{
    destroySurface();
    createSurface();
}

void SkiaSalGraphicsImpl::createSurface()
{
    if (isOffscreen())
        createOffscreenSurface();
    else
        createWindowSurface();
    mSurface->getCanvas()->save(); // see SetClipRegion()
    mClipRegion = vcl::Region(tools::Rectangle(0, 0, GetWidth(), GetHeight()));

    // We don't want to be swapping before we've painted.
    mFlush->Stop();
    mFlush->SetPriority(TaskPriority::POST_PAINT);
}

void SkiaSalGraphicsImpl::createWindowSurface()
{
    assert(!isOffscreen());
    assert(!mSurface);
    assert(!mWindowContext);
    createWindowContext();
    if (mWindowContext)
        mSurface = mWindowContext->getBackbufferSurface();
    if (!mSurface)
    {
        switch (SkiaHelper::renderMethodToUse())
        {
            case SkiaHelper::RenderVulkan:
                SAL_WARN("vcl.skia", "cannot create Vulkan GPU window surface, disabling Vulkan");
                // fall back to raster
                SkiaHelper::disableRenderMethod(SkiaHelper::RenderVulkan);
                destroySurface(); // destroys also WindowContext
                return createWindowSurface(); // try again
            case SkiaHelper::RenderRaster:
                abort(); // this should not really happen
        }
    }
    assert((mSurface->getCanvas()->getGrContext() != nullptr) == mIsGPU);
#ifdef DBG_UTIL
    SkiaHelper::prefillSurface(mSurface);
#endif
}

void SkiaSalGraphicsImpl::createOffscreenSurface()
{
    assert(isOffscreen());
    assert(!mSurface);
    assert(!mWindowContext);
    switch (SkiaHelper::renderMethodToUse())
    {
        case SkiaHelper::RenderVulkan:
        {
            GrContext* grContext = SkiaHelper::getSharedGrContext();
            // We may not get a GrContext if called before any onscreen window is created.
            if (!grContext)
            {
                SAL_INFO("vcl.skia",
                         "creating Vulkan offscreen GPU surface before any window exists");
                // Create temporary WindowContext with no window. That will fail,
                // but it will initialize the shared GrContext.
                createWindowContext();
                // This will use the temporarily created context.
                grContext = SkiaHelper::getSharedGrContext();
                // Destroy the temporary WindowContext.
                destroySurface();
            }
            if (grContext)
            {
                mSurface = SkiaHelper::createSkSurface(GetWidth(), GetHeight());
                assert(mSurface);
                assert(mSurface->getCanvas()->getGrContext()); // is GPU-backed
                mIsGPU = true;
                return;
            }
            SAL_WARN("vcl.skia", "cannot create Vulkan offscreen GPU surface, disabling Vulkan");
            SkiaHelper::disableRenderMethod(SkiaHelper::RenderVulkan);
            break;
        }
        default:
            break;
    }
    // Create raster surface as a fallback.
    mSurface = SkiaHelper::createSkSurface(GetWidth(), GetHeight());
    assert(mSurface);
    assert(!mSurface->getCanvas()->getGrContext()); // is not GPU-backed
    mIsGPU = false;
}

void SkiaSalGraphicsImpl::destroySurface()
{
    if (mSurface)
    {
        // check setClipRegion() invariant
        assert(mSurface->getCanvas()->getSaveCount() == 2);
        // if this fails, something forgot to use SkAutoCanvasRestore
        assert(mSurface->getCanvas()->getTotalMatrix().isIdentity());
    }
    // If we use e.g. Vulkan, we must destroy the surface before the context,
    // otherwise destroying the surface will reference the context. This is
    // handled by calling destroySurface() before destroying the context.
    // However we also need to flush the surface before destroying it,
    // otherwise when destroying the context later there still could be queued
    // commands referring to the surface data. This is probably a Skia bug,
    // but work around it here.
    if (mSurface)
        mSurface->flush();
    mSurface.reset();
    mWindowContext.reset();
    mIsGPU = false;
}

void SkiaSalGraphicsImpl::DeInit() { destroySurface(); }

void SkiaSalGraphicsImpl::preDraw()
{
    checkSurface();
    assert(!mXorMode || mXorExtents.isEmpty()); // must be reset in postDraw()
}

void SkiaSalGraphicsImpl::postDraw()
{
    if (mXorMode)
    {
        // Apply the result from the temporary bitmap manually. This is indeed
        // slow, but it doesn't seem to be needed often and can be optimized
        // in each operation by setting mXorExtents to the area that should be
        // updated.
        if (mXorExtents.isEmpty())
            mXorExtents = SkRect::MakeXYWH(0, 0, mSurface->width(), mSurface->height());
        else
        {
            // Make slightly larger, just in case (rounding, antialiasing,...).
            mXorExtents.outset(2, 2);
            mXorExtents.intersect(SkRect::MakeXYWH(0, 0, mSurface->width(), mSurface->height()));
        }
        SAL_INFO("vcl.skia", "applyxor("
                                 << this << "): "
                                 << tools::Rectangle(mXorExtents.left(), mXorExtents.top(),
                                                     mXorExtents.right(), mXorExtents.bottom()));
        // Copy the surface contents to another pixmap.
        SkBitmap surfaceBitmap;
        // Use unpremultiplied alpha format, so that we do not have to do the conversions to get
        // the RGB and back (Skia will do it when converting, but it'll be presumably faster at it).
        if (!surfaceBitmap.tryAllocPixels(
                mSurface->imageInfo().makeAlphaType(kUnpremul_SkAlphaType)))
            abort();
        SkPaint paint;
        paint.setBlendMode(SkBlendMode::kSrc); // copy as is
        SkCanvas canvas(surfaceBitmap);
        canvas.drawImageRect(mSurface->makeImageSnapshot(), mXorExtents, mXorExtents, &paint);
        // xor to surfaceBitmap
        assert(surfaceBitmap.info().alphaType() == kUnpremul_SkAlphaType);
        assert(mXorBitmap.info().alphaType() == kUnpremul_SkAlphaType);
        assert(surfaceBitmap.bytesPerPixel() == 4);
        assert(mXorBitmap.bytesPerPixel() == 4);
        for (int y = mXorExtents.top(); y < mXorExtents.bottom(); ++y)
        {
            uint8_t* data = static_cast<uint8_t*>(surfaceBitmap.getAddr(mXorExtents.x(), y));
            const uint8_t* xordata = static_cast<uint8_t*>(mXorBitmap.getAddr(mXorExtents.x(), y));
            for (int x = 0; x < mXorExtents.width(); ++x)
            {
                *data++ ^= *xordata++;
                *data++ ^= *xordata++;
                *data++ ^= *xordata++;
                // alpha is not xor-ed
                data++;
                xordata++;
            }
        }
        surfaceBitmap.notifyPixelsChanged();
        mSurface->getCanvas()->drawBitmapRect(surfaceBitmap, mXorExtents, mXorExtents, &paint);
        mXorCanvas.reset();
        mXorBitmap.reset();
        mXorExtents.setEmpty();
    }
    if (!isOffscreen())
    {
        if (!Application::IsInExecute())
            performFlush(); // otherwise nothing would trigger idle rendering
        else if (!mFlush->IsActive())
            mFlush->Start();
    }
    // Skia (at least when using Vulkan) queues drawing commands and executes them only later.
    // But some operations may queue way too much data to draw, leading to Vulkan getting out of memory,
    // which at least on Linux leads to driver problems affecting even the whole X11 session.
    // One such problematic operation may be drawBitmap(SkBitmap), which is used by SkiaX11CairoTextRender
    // to draw text, which is internally done by creating the SkBitmap from cairo surface data. Apparently
    // the cairo surface's size matches the size of the destination (window), which may be large,
    // and each text drawing allocates a new surface (and thus SkBitmap). So we may end up queueing up
    // millions of pixels of bitmap data. So force a flush if such a possibly problematic operation
    // has queued up too much data.
    if (mPendingPixelsToFlush > 10 * 1024 * 1024)
    {
        mSurface->flush();
        mPendingPixelsToFlush = 0;
    }
}

// VCL can sometimes resize us without telling us, update the surface if needed.
// Also create the surface on demand if it has not been created yet (it is a waste
// to create it in Init() if it gets recreated later anyway).
void SkiaSalGraphicsImpl::checkSurface()
{
    if (!mSurface)
    {
        recreateSurface();
        SAL_INFO("vcl.skia",
                 "create(" << this << "): " << Size(mSurface->width(), mSurface->height()));
    }
    else if (GetWidth() != mSurface->width() || GetHeight() != mSurface->height())
    {
        if (!avoidRecreateByResize())
        {
            Size oldSize(mSurface->width(), mSurface->height());
            recreateSurface();
            SAL_INFO("vcl.skia", "recreate(" << this << "): old " << oldSize << " new "
                                             << Size(mSurface->width(), mSurface->height())
                                             << " requested " << Size(GetWidth(), GetHeight()));
        }
    }
}

bool SkiaSalGraphicsImpl::setClipRegion(const vcl::Region& region)
{
    if (mClipRegion == region)
        return true;
    mClipRegion = region;
    checkSurface();
    SAL_INFO("vcl.skia", "setclipregion(" << this << "): " << region);
    SkCanvas* canvas = mSurface->getCanvas();
    // SkCanvas::clipRegion() can only further reduce the clip region,
    // but we need to set the given region, which may extend it.
    // So handle that by always having the full clip region saved on the stack
    // and always go back to that. SkCanvas::restore() only affects the clip
    // and the matrix.
    assert(canvas->getSaveCount() == 2); // = there is just one save()
    canvas->restore();
    canvas->save();
    setCanvasClipRegion(canvas, region);
    return true;
}

void SkiaSalGraphicsImpl::setCanvasClipRegion(SkCanvas* canvas, const vcl::Region& region)
{
    SkPath path;
    // Handle polygons last, since rectangle->polygon area conversions
    // are problematic (see addPolygonToPath() comment).
    if (region.getRegionBand())
    {
        RectangleVector rectangles;
        region.GetRegionRectangles(rectangles);
        for (const tools::Rectangle& rectangle : rectangles)
            path.addRect(SkRect::MakeXYWH(rectangle.getX(), rectangle.getY(), rectangle.GetWidth(),
                                          rectangle.GetHeight()));
    }
    else if (!region.IsEmpty())
    {
        addPolyPolygonToPath(region.GetAsB2DPolyPolygon(), path);
    }
    path.setFillType(SkPathFillType::kEvenOdd);
    canvas->clipPath(path);
}

void SkiaSalGraphicsImpl::ResetClipRegion()
{
    setClipRegion(vcl::Region(tools::Rectangle(0, 0, GetWidth(), GetHeight())));
}

const vcl::Region& SkiaSalGraphicsImpl::getClipRegion() const { return mClipRegion; }

sal_uInt16 SkiaSalGraphicsImpl::GetBitCount() const { return 32; }

long SkiaSalGraphicsImpl::GetGraphicsWidth() const { return GetWidth(); }

void SkiaSalGraphicsImpl::SetLineColor() { mLineColor = SALCOLOR_NONE; }

void SkiaSalGraphicsImpl::SetLineColor(Color nColor) { mLineColor = nColor; }

void SkiaSalGraphicsImpl::SetFillColor() { mFillColor = SALCOLOR_NONE; }

void SkiaSalGraphicsImpl::SetFillColor(Color nColor) { mFillColor = nColor; }

void SkiaSalGraphicsImpl::SetXORMode(bool set, bool)
{
    mXorMode = set;
    if (mXorMode)
        mXorExtents.setEmpty();
}

SkCanvas* SkiaSalGraphicsImpl::getXorCanvas()
{
    assert(mXorMode);
    // Skia does not implement xor drawing, so we need to handle it manually by redirecting
    // to a temporary SkBitmap and then doing the xor operation on the data ourselves.
    // There's no point in using SkSurface for GPU, we'd immediately need to get the pixels back.
    if (!mXorCanvas)
    {
        // Use unpremultiplied alpha (see xor applying in PostDraw()).
        if (!mXorBitmap.tryAllocPixels(mSurface->imageInfo().makeAlphaType(kUnpremul_SkAlphaType)))
            abort();
        mXorBitmap.eraseARGB(0, 0, 0, 0);
        mXorCanvas = std::make_unique<SkCanvas>(mXorBitmap);
        setCanvasClipRegion(mXorCanvas.get(), mClipRegion);
    }
    return mXorCanvas.get();
}

void SkiaSalGraphicsImpl::SetROPLineColor(SalROPColor nROPColor)
{
    switch (nROPColor)
    {
        case SalROPColor::N0:
            mLineColor = Color(0, 0, 0);
            break;
        case SalROPColor::N1:
            mLineColor = Color(0xff, 0xff, 0xff);
            break;
        case SalROPColor::Invert:
            mLineColor = Color(0xff, 0xff, 0xff);
            break;
    }
}

void SkiaSalGraphicsImpl::SetROPFillColor(SalROPColor nROPColor)
{
    switch (nROPColor)
    {
        case SalROPColor::N0:
            mFillColor = Color(0, 0, 0);
            break;
        case SalROPColor::N1:
            mFillColor = Color(0xff, 0xff, 0xff);
            break;
        case SalROPColor::Invert:
            mFillColor = Color(0xff, 0xff, 0xff);
            break;
    }
}

void SkiaSalGraphicsImpl::drawPixel(long nX, long nY) { drawPixel(nX, nY, mLineColor); }

void SkiaSalGraphicsImpl::drawPixel(long nX, long nY, Color nColor)
{
    if (nColor == SALCOLOR_NONE)
        return;
    preDraw();
    SAL_INFO("vcl.skia", "drawpixel(" << this << "): " << Point(nX, nY) << ":" << nColor);
    SkPaint paint;
    paint.setColor(toSkColor(nColor));
    // Apparently drawPixel() is actually expected to set the pixel and not draw it.
    paint.setBlendMode(SkBlendMode::kSrc); // set as is, including alpha
    getDrawCanvas()->drawPoint(toSkX(nX), toSkY(nY), paint);
    if (mXorMode) // limit xor area update
        mXorExtents = SkRect::MakeXYWH(nX, nY, 1, 1);
    postDraw();
}

void SkiaSalGraphicsImpl::drawLine(long nX1, long nY1, long nX2, long nY2)
{
    if (mLineColor == SALCOLOR_NONE)
        return;
    preDraw();
    SAL_INFO("vcl.skia", "drawline(" << this << "): " << Point(nX1, nY1) << "->" << Point(nX2, nY2)
                                     << ":" << mLineColor);
    SkPaint paint;
    paint.setColor(toSkColor(mLineColor));
    paint.setAntiAlias(mParent.getAntiAliasB2DDraw());
    // Raster has better results if shifted by 0.25 (unlike the 0.5 done by toSkX/toSkY).
    if (!isGPU())
        getDrawCanvas()->drawLine(nX1 + 0.25, nY1 + 0.25, nX2 + 0.25, nY2 + 0.25, paint);
    else
        getDrawCanvas()->drawLine(toSkX(nX1), toSkY(nY1), toSkX(nX2), toSkY(nY2), paint);
    if (mXorMode) // limit xor area update
        mXorExtents = SkRect::MakeLTRB(nX1, nY1, nX2 + 1, nY2 + 1);
    postDraw();
}

void SkiaSalGraphicsImpl::privateDrawAlphaRect(long nX, long nY, long nWidth, long nHeight,
                                               double fTransparency, bool blockAA)
{
    preDraw();
    SAL_INFO("vcl.skia", "privatedrawrect(" << this << "): " << Point(nX, nY) << "/"
                                            << Size(nWidth, nHeight) << ":" << mLineColor << ":"
                                            << mFillColor << ":" << fTransparency);
    SkCanvas* canvas = getDrawCanvas();
    SkPaint paint;
    paint.setAntiAlias(!blockAA && mParent.getAntiAliasB2DDraw());
    if (mFillColor != SALCOLOR_NONE)
    {
        paint.setColor(toSkColorWithTransparency(mFillColor, fTransparency));
        paint.setStyle(SkPaint::kFill_Style);
        canvas->drawIRect(SkIRect::MakeXYWH(nX, nY, nWidth, nHeight), paint);
    }
    if (mLineColor != SALCOLOR_NONE)
    {
        paint.setColor(toSkColorWithTransparency(mLineColor, fTransparency));
        paint.setStyle(SkPaint::kStroke_Style);
        canvas->drawIRect(SkIRect::MakeXYWH(nX, nY, nWidth - 1, nHeight - 1), paint);
    }
    if (mXorMode) // limit xor area update
        mXorExtents = SkRect::MakeXYWH(nX, nY, nWidth, nHeight);
    postDraw();
}

void SkiaSalGraphicsImpl::drawRect(long nX, long nY, long nWidth, long nHeight)
{
    privateDrawAlphaRect(nX, nY, nWidth, nHeight, 0.0, true);
}

void SkiaSalGraphicsImpl::drawPolyLine(sal_uInt32 nPoints, const SalPoint* pPtAry)
{
    basegfx::B2DPolygon aPolygon;
    aPolygon.append(basegfx::B2DPoint(pPtAry->mnX, pPtAry->mnY), nPoints);
    for (sal_uInt32 i = 1; i < nPoints; ++i)
        aPolygon.setB2DPoint(i, basegfx::B2DPoint(pPtAry[i].mnX, pPtAry[i].mnY));
    aPolygon.setClosed(false);

    drawPolyLine(basegfx::B2DHomMatrix(), aPolygon, 0.0, basegfx::B2DVector(1.0, 1.0),
                 basegfx::B2DLineJoin::Miter, css::drawing::LineCap_BUTT,
                 basegfx::deg2rad(15.0) /*default*/, false);
}

void SkiaSalGraphicsImpl::drawPolygon(sal_uInt32 nPoints, const SalPoint* pPtAry)
{
    basegfx::B2DPolygon aPolygon;
    aPolygon.append(basegfx::B2DPoint(pPtAry->mnX, pPtAry->mnY), nPoints);
    for (sal_uInt32 i = 1; i < nPoints; ++i)
        aPolygon.setB2DPoint(i, basegfx::B2DPoint(pPtAry[i].mnX, pPtAry[i].mnY));

    drawPolyPolygon(basegfx::B2DHomMatrix(), basegfx::B2DPolyPolygon(aPolygon), 0.0);
}

void SkiaSalGraphicsImpl::drawPolyPolygon(sal_uInt32 nPoly, const sal_uInt32* pPoints,
                                          PCONSTSALPOINT* pPtAry)
{
    basegfx::B2DPolyPolygon aPolyPolygon;
    for (sal_uInt32 nPolygon = 0; nPolygon < nPoly; ++nPolygon)
    {
        sal_uInt32 nPoints = pPoints[nPolygon];
        if (nPoints)
        {
            PCONSTSALPOINT pSalPoints = pPtAry[nPolygon];
            basegfx::B2DPolygon aPolygon;
            aPolygon.append(basegfx::B2DPoint(pSalPoints->mnX, pSalPoints->mnY), nPoints);
            for (sal_uInt32 i = 1; i < nPoints; ++i)
                aPolygon.setB2DPoint(i, basegfx::B2DPoint(pSalPoints[i].mnX, pSalPoints[i].mnY));

            aPolyPolygon.append(aPolygon);
        }
    }

    drawPolyPolygon(basegfx::B2DHomMatrix(), aPolyPolygon, 0.0);
}

bool SkiaSalGraphicsImpl::drawPolyPolygon(const basegfx::B2DHomMatrix& rObjectToDevice,
                                          const basegfx::B2DPolyPolygon& rPolyPolygon,
                                          double fTransparency)
{
    const bool bHasFill(mFillColor != SALCOLOR_NONE);
    const bool bHasLine(mLineColor != SALCOLOR_NONE);

    if (rPolyPolygon.count() == 0 || !(bHasFill || bHasLine) || fTransparency < 0.0
        || fTransparency >= 1.0)
        return true;

    preDraw();

    SkPath aPath;
    basegfx::B2DPolyPolygon aPolyPolygon(rPolyPolygon);
    aPolyPolygon.transform(rObjectToDevice);
    SAL_INFO("vcl.skia", "drawpolypolygon(" << this << "): " << aPolyPolygon << ":" << mLineColor
                                            << ":" << mFillColor);
    addPolyPolygonToPath(aPolyPolygon, aPath);
    aPath.setFillType(SkPathFillType::kEvenOdd);

    SkPaint aPaint;
    aPaint.setAntiAlias(mParent.getAntiAliasB2DDraw());
    if (mFillColor != SALCOLOR_NONE)
    {
        aPaint.setColor(toSkColorWithTransparency(mFillColor, fTransparency));
        aPaint.setStyle(SkPaint::kFill_Style);
        getDrawCanvas()->drawPath(aPath, aPaint);
    }
    if (mLineColor != SALCOLOR_NONE)
    {
        // Raster has better results if shifted by 0.25 (unlike the 0.5 done by toSkX/toSkY).
        if (!isGPU())
            aPath.offset(0.25, 0.25, nullptr);
        else // Apply the same adjustment as toSkX()/toSkY() do.
            aPath.offset(0.5, 0.5, nullptr);
        aPaint.setColor(toSkColorWithTransparency(mLineColor, fTransparency));
        aPaint.setStyle(SkPaint::kStroke_Style);
        getDrawCanvas()->drawPath(aPath, aPaint);
    }
    if (mXorMode) // limit xor area update
        mXorExtents = aPath.getBounds();
    postDraw();
    return true;
}

bool SkiaSalGraphicsImpl::drawPolyLine(const basegfx::B2DHomMatrix& rObjectToDevice,
                                       const basegfx::B2DPolygon& rPolyLine, double fTransparency,
                                       const basegfx::B2DVector& rLineWidths,
                                       basegfx::B2DLineJoin eLineJoin,
                                       css::drawing::LineCap eLineCap, double fMiterMinimumAngle,
                                       bool bPixelSnapHairline)
{
    if (rPolyLine.count() == 0 || fTransparency < 0.0 || fTransparency >= 1.0
        || mLineColor == SALCOLOR_NONE)
        return true;

    preDraw();
    SAL_INFO("vcl.skia", "drawpolyline(" << this << "): " << rPolyLine << ":" << mLineColor);

    // need to check/handle LineWidth when ObjectToDevice transformation is used
    const basegfx::B2DVector aDeviceLineWidths(rObjectToDevice * rLineWidths);
    const bool bCorrectLineWidth(aDeviceLineWidths.getX() < 1.0 && rLineWidths.getX() >= 1.0);
    const basegfx::B2DVector aLineWidths(bCorrectLineWidth ? rLineWidths : aDeviceLineWidths);

    // Skia does not support B2DLineJoin::NONE; return false to use
    // the fallback (own geometry preparation),
    // linejoin-mode and thus the above only applies to "fat" lines.
    if ((basegfx::B2DLineJoin::NONE == eLineJoin) && (aLineWidths.getX() > 1.3))
        return false;

    // Transform to DeviceCoordinates, get DeviceLineWidth, execute PixelSnapHairline
    basegfx::B2DPolygon aPolyLine(rPolyLine);
    aPolyLine.transform(rObjectToDevice);
    if (bPixelSnapHairline)
        aPolyLine = basegfx::utils::snapPointsOfHorizontalOrVerticalEdges(aPolyLine);

    // Setup Line Join
    SkPaint::Join eSkLineJoin = SkPaint::kMiter_Join;
    switch (eLineJoin)
    {
        case basegfx::B2DLineJoin::Bevel:
            eSkLineJoin = SkPaint::kBevel_Join;
            break;
        case basegfx::B2DLineJoin::Round:
            eSkLineJoin = SkPaint::kRound_Join;
            break;
        case basegfx::B2DLineJoin::NONE:
        case basegfx::B2DLineJoin::Miter:
            eSkLineJoin = SkPaint::kMiter_Join;
            break;
    }

    // convert miter minimum angle to miter limit
    double fMiterLimit = 1.0 / std::sin(fMiterMinimumAngle / 2.0);

    // Setup Line Cap
    SkPaint::Cap eSkLineCap(SkPaint::kButt_Cap);

    switch (eLineCap)
    {
        case css::drawing::LineCap_ROUND:
            eSkLineCap = SkPaint::kRound_Cap;
            break;
        case css::drawing::LineCap_SQUARE:
            eSkLineCap = SkPaint::kSquare_Cap;
            break;
        default: // css::drawing::LineCap_BUTT:
            eSkLineCap = SkPaint::kButt_Cap;
            break;
    }

    SkPaint aPaint;
    aPaint.setStyle(SkPaint::kStroke_Style);
    aPaint.setStrokeCap(eSkLineCap);
    aPaint.setStrokeJoin(eSkLineJoin);
    aPaint.setColor(toSkColorWithTransparency(mLineColor, fTransparency));
    aPaint.setStrokeMiter(fMiterLimit);
    aPaint.setStrokeWidth(aLineWidths.getX());
    aPaint.setAntiAlias(mParent.getAntiAliasB2DDraw());

    SkPath aPath;
    addPolygonToPath(aPolyLine, aPath);
    aPath.setFillType(SkPathFillType::kEvenOdd);
    // Apply the same adjustment as toSkX()/toSkY() do. Do it here even in the non-GPU
    // case as it seems to produce better results.
    aPath.offset(0.5, 0.5, nullptr);
    getDrawCanvas()->drawPath(aPath, aPaint);
    if (mXorMode) // limit xor area update
        mXorExtents = aPath.getBounds();
    postDraw();

    return true;
}

bool SkiaSalGraphicsImpl::drawPolyLineBezier(sal_uInt32, const SalPoint*, const PolyFlags*)
{
    // TODO?
    return false;
}

bool SkiaSalGraphicsImpl::drawPolygonBezier(sal_uInt32, const SalPoint*, const PolyFlags*)
{
    // TODO?
    return false;
}

bool SkiaSalGraphicsImpl::drawPolyPolygonBezier(sal_uInt32, const sal_uInt32*,
                                                const SalPoint* const*, const PolyFlags* const*)
{
    // TODO?
    return false;
}

void SkiaSalGraphicsImpl::copyArea(long nDestX, long nDestY, long nSrcX, long nSrcY, long nSrcWidth,
                                   long nSrcHeight, bool /*bWindowInvalidate*/)
{
    if (nDestX == nSrcX && nDestY == nSrcY)
        return;
    preDraw();
    SAL_INFO("vcl.skia", "copyarea(" << this << "): " << Point(nSrcX, nSrcY) << "->"
                                     << Point(nDestX, nDestY) << "/"
                                     << Size(nSrcWidth, nSrcHeight));
    // Do not use makeImageSnapshot(rect), as that one may make a needless data copy.
    sk_sp<SkImage> image = mSurface->makeImageSnapshot();
    SkPaint paint;
    paint.setBlendMode(SkBlendMode::kSrc); // copy as is, including alpha
    getDrawCanvas()->drawImageRect(image, SkIRect::MakeXYWH(nSrcX, nSrcY, nSrcWidth, nSrcHeight),
                                   SkRect::MakeXYWH(nDestX, nDestY, nSrcWidth, nSrcHeight), &paint);
    if (mXorMode) // limit xor area update
        mXorExtents = SkRect::MakeXYWH(nDestX, nDestY, nSrcWidth, nSrcHeight);
    postDraw();
}

void SkiaSalGraphicsImpl::copyBits(const SalTwoRect& rPosAry, SalGraphics* pSrcGraphics)
{
    preDraw();
    SkiaSalGraphicsImpl* src;
    if (pSrcGraphics)
    {
        assert(dynamic_cast<SkiaSalGraphicsImpl*>(pSrcGraphics->GetImpl()));
        src = static_cast<SkiaSalGraphicsImpl*>(pSrcGraphics->GetImpl());
        src->checkSurface();
    }
    else
        src = this;
    SAL_INFO("vcl.skia", "copybits(" << this << "): (" << src << "):" << rPosAry);
    // Do not use makeImageSnapshot(rect), as that one may make a needless data copy.
    sk_sp<SkImage> image = src->mSurface->makeImageSnapshot();
    SkPaint paint;
    paint.setBlendMode(SkBlendMode::kSrc); // copy as is, including alpha
    getDrawCanvas()->drawImageRect(
        image,
        SkIRect::MakeXYWH(rPosAry.mnSrcX, rPosAry.mnSrcY, rPosAry.mnSrcWidth, rPosAry.mnSrcHeight),
        SkRect::MakeXYWH(rPosAry.mnDestX, rPosAry.mnDestY, rPosAry.mnDestWidth,
                         rPosAry.mnDestHeight),
        &paint);
    if (mXorMode) // limit xor area update
        mXorExtents = SkRect::MakeXYWH(rPosAry.mnDestX, rPosAry.mnDestY, rPosAry.mnDestWidth,
                                       rPosAry.mnDestHeight);
    postDraw();
}

bool SkiaSalGraphicsImpl::blendBitmap(const SalTwoRect& rPosAry, const SalBitmap& rBitmap)
{
    if (checkInvalidSourceOrDestination(rPosAry))
        return false;

    assert(dynamic_cast<const SkiaSalBitmap*>(&rBitmap));
    const SkiaSalBitmap& rSkiaBitmap = static_cast<const SkiaSalBitmap&>(rBitmap);
    // This is used by VirtualDevice in the alpha mode for the "alpha" layer which
    // is actually one-minus-alpha (opacity). Therefore white=0xff=transparent,
    // black=0x00=opaque. So the result is transparent only if both the inputs
    // are transparent. Since for blending operations white=1.0 and black=0.0,
    // kMultiply should handle exactly that (transparent*transparent=transparent,
    // opaque*transparent=opaque). And guessing from the "floor" in TYPE_BLEND in opengl's
    // combinedTextureFragmentShader.glsl, the layer is not even alpha values but
    // simply yes-or-no mask.
    // See also blendAlphaBitmap().
    drawImage(rPosAry, rSkiaBitmap.GetSkImage(), SkBlendMode::kMultiply);
    return true;
}

bool SkiaSalGraphicsImpl::blendAlphaBitmap(const SalTwoRect& rPosAry,
                                           const SalBitmap& rSourceBitmap,
                                           const SalBitmap& rMaskBitmap,
                                           const SalBitmap& rAlphaBitmap)
{
    if (checkInvalidSourceOrDestination(rPosAry))
        return false;

    assert(dynamic_cast<const SkiaSalBitmap*>(&rSourceBitmap));
    assert(dynamic_cast<const SkiaSalBitmap*>(&rMaskBitmap));
    assert(dynamic_cast<const SkiaSalBitmap*>(&rAlphaBitmap));

    sk_sp<SkSurface> tmpSurface = SkiaHelper::createSkSurface(rSourceBitmap.GetSize());
    if (!tmpSurface)
        return false;

    const SkiaSalBitmap& rSkiaSourceBitmap = static_cast<const SkiaSalBitmap&>(rSourceBitmap);
    const SkiaSalBitmap& rSkiaMaskBitmap = static_cast<const SkiaSalBitmap&>(rMaskBitmap);
    const SkiaSalBitmap& rSkiaAlphaBitmap = static_cast<const SkiaSalBitmap&>(rAlphaBitmap);

    // This was originally implemented for the OpenGL drawing method and it is poorly documented.
    // The source and mask bitmaps are the usual data and alpha bitmaps, and 'alpha'
    // is the "alpha" layer of the VirtualDevice (the alpha in VirtualDevice is also stored
    // as a separate bitmap). Now I understand it correctly these two alpha masks first need
    // to be combined into the actual alpha mask to be used. The formula for TYPE_BLEND
    // in opengl's combinedTextureFragmentShader.glsl is
    // "result_alpha = 1.0 - (1.0 - floor(alpha)) * mask".
    // See also blendBitmap().
    SkCanvas* aCanvas = tmpSurface->getCanvas();
    aCanvas->clear(SK_ColorTRANSPARENT);
    SkPaint aPaint;
    // First copy the mask as is.
    aPaint.setBlendMode(SkBlendMode::kSrc);
    aCanvas->drawImage(rSkiaMaskBitmap.GetAlphaSkImage(), 0, 0, &aPaint);
    // Do the "1 - alpha" (no idea how to do "floor", but hopefully not needed in practice).
    aPaint.setBlendMode(SkBlendMode::kDstOut);
    aCanvas->drawImage(rSkiaAlphaBitmap.GetAlphaSkImage(), 0, 0, &aPaint);
    // And now draw the bitmap with "1 - x", where x is the "( 1 - alpha ) * mask".
    aPaint.setBlendMode(SkBlendMode::kSrcOut);
    aCanvas->drawImage(rSkiaSourceBitmap.GetSkImage(), 0, 0, &aPaint);

    drawImage(rPosAry, tmpSurface->makeImageSnapshot());
    return true;
}

void SkiaSalGraphicsImpl::drawBitmap(const SalTwoRect& rPosAry, const SalBitmap& rSalBitmap)
{
    if (checkInvalidSourceOrDestination(rPosAry))
        return;

    assert(dynamic_cast<const SkiaSalBitmap*>(&rSalBitmap));
    const SkiaSalBitmap& rSkiaSourceBitmap = static_cast<const SkiaSalBitmap&>(rSalBitmap);

    drawImage(rPosAry, rSkiaSourceBitmap.GetSkImage());
}

void SkiaSalGraphicsImpl::drawBitmap(const SalTwoRect& rPosAry, const SalBitmap& rSalBitmap,
                                     const SalBitmap& rMaskBitmap)
{
    drawAlphaBitmap(rPosAry, rSalBitmap, rMaskBitmap);
}

void SkiaSalGraphicsImpl::drawMask(const SalTwoRect& rPosAry, const SalBitmap& rSalBitmap,
                                   Color nMaskColor)
{
    assert(dynamic_cast<const SkiaSalBitmap*>(&rSalBitmap));
    drawMask(rPosAry, static_cast<const SkiaSalBitmap&>(rSalBitmap).GetAlphaSkImage(), nMaskColor);
}

void SkiaSalGraphicsImpl::drawMask(const SalTwoRect& rPosAry, const sk_sp<SkImage>& rImage,
                                   Color nMaskColor)
{
    SAL_INFO("vcl.skia", "drawmask(" << this << "): " << rPosAry << ":" << nMaskColor);
    sk_sp<SkSurface> tmpSurface = SkiaHelper::createSkSurface(rImage->width(), rImage->height());
    assert(tmpSurface);
    SkCanvas* canvas = tmpSurface->getCanvas();
    canvas->clear(toSkColor(nMaskColor));
    SkPaint paint;
    // Draw the color with the given mask.
    // TODO figure out the right blend mode to avoid the temporary surface
    paint.setBlendMode(SkBlendMode::kDstOut);
    canvas->drawImage(rImage, 0, 0, &paint);

    drawImage(rPosAry, tmpSurface->makeImageSnapshot());
}

std::shared_ptr<SalBitmap> SkiaSalGraphicsImpl::getBitmap(long nX, long nY, long nWidth,
                                                          long nHeight)
{
    checkSurface();
    SAL_INFO("vcl.skia",
             "getbitmap(" << this << "): " << Point(nX, nY) << "/" << Size(nWidth, nHeight));
    mSurface->getCanvas()->flush();
    // TODO makeImageSnapshot(rect) may copy the data, which may be a waste if this is used
    // e.g. for VirtualDevice's lame alpha blending, in which case the image will eventually end up
    // in blendAlphaBitmap(), where we could simply use the proper rect of the image.
    sk_sp<SkImage> image = mSurface->makeImageSnapshot(SkIRect::MakeXYWH(nX, nY, nWidth, nHeight));
    return std::make_shared<SkiaSalBitmap>(image);
}

Color SkiaSalGraphicsImpl::getPixel(long nX, long nY)
{
    checkSurface();
    SAL_INFO("vcl.skia", "getpixel(" << this << "): " << Point(nX, nY));
    mSurface->getCanvas()->flush();
    // This is presumably slow, but getPixel() should be generally used only by unit tests.
    SkBitmap bitmap;
    if (!bitmap.tryAllocN32Pixels(GetWidth(), GetHeight()))
        abort();
    if (!mSurface->readPixels(bitmap, 0, 0))
        abort();
    return fromSkColor(bitmap.getColor(nX, nY));
}

void SkiaSalGraphicsImpl::invert(basegfx::B2DPolygon const& rPoly, SalInvert eFlags)
{
    preDraw();
    SAL_INFO("vcl.skia", "invert(" << this << "): " << rPoly << ":" << int(eFlags));
    // TrackFrame just inverts a dashed path around the polygon
    if (eFlags == SalInvert::TrackFrame)
    {
        SkPath aPath;
        addPolygonToPath(rPoly, aPath);
        aPath.setFillType(SkPathFillType::kEvenOdd);
        // TrackFrame is not supposed to paint outside of the polygon (usually rectangle),
        // but wider stroke width usually results in that, so ensure the requirement
        // by clipping.
        SkAutoCanvasRestore autoRestore(getDrawCanvas(), true);
        getDrawCanvas()->clipRect(aPath.getBounds(), SkClipOp::kIntersect, false);
        SkPaint aPaint;
        aPaint.setStrokeWidth(2);
        float intervals[] = { 4.0f, 4.0f };
        aPaint.setStyle(SkPaint::kStroke_Style);
        aPaint.setPathEffect(SkDashPathEffect::Make(intervals, SK_ARRAY_COUNT(intervals), 0));
        aPaint.setColor(SkColorSetARGB(255, 255, 255, 255));
        aPaint.setBlendMode(SkBlendMode::kDifference);

        getDrawCanvas()->drawPath(aPath, aPaint);
        if (mXorMode) // limit xor area update
            mXorExtents = aPath.getBounds();
    }
    else
    {
        SkPath aPath;
        addPolygonToPath(rPoly, aPath);
        aPath.setFillType(SkPathFillType::kEvenOdd);
        SkPaint aPaint;
        aPaint.setColor(SkColorSetARGB(255, 255, 255, 255));
        aPaint.setStyle(SkPaint::kFill_Style);
        aPaint.setBlendMode(SkBlendMode::kDifference);

        // N50 inverts in 4x4 checker pattern
        if (eFlags == SalInvert::N50)
        {
            // This creates 4x4 checker pattern bitmap
            // TODO Use SkiaHelper::createSkSurface() and cache the image
            SkBitmap aBitmap;
            aBitmap.allocN32Pixels(4, 4);
            const SkPMColor white = SkPreMultiplyARGB(0xFF, 0xFF, 0xFF, 0xFF);
            const SkPMColor black = SkPreMultiplyARGB(0xFF, 0x00, 0x00, 0x00);
            SkPMColor* scanline;
            scanline = aBitmap.getAddr32(0, 0);
            *scanline++ = white;
            *scanline++ = white;
            *scanline++ = black;
            *scanline++ = black;
            scanline = aBitmap.getAddr32(0, 1);
            *scanline++ = white;
            *scanline++ = white;
            *scanline++ = black;
            *scanline++ = black;
            scanline = aBitmap.getAddr32(0, 2);
            *scanline++ = black;
            *scanline++ = black;
            *scanline++ = white;
            *scanline++ = white;
            scanline = aBitmap.getAddr32(0, 3);
            *scanline++ = black;
            *scanline++ = black;
            *scanline++ = white;
            *scanline++ = white;
            aBitmap.setImmutable();
            // The bitmap is repeated in both directions the checker pattern is as big
            // as the polygon (usually rectangle)
            aPaint.setShader(aBitmap.makeShader(SkTileMode::kRepeat, SkTileMode::kRepeat));
        }

        getDrawCanvas()->drawPath(aPath, aPaint);
        if (mXorMode) // limit xor area update
            mXorExtents = aPath.getBounds();
    }
    postDraw();
}

void SkiaSalGraphicsImpl::invert(long nX, long nY, long nWidth, long nHeight, SalInvert eFlags)
{
    basegfx::B2DRectangle aRectangle(nX, nY, nX + nWidth, nY + nHeight);
    auto aRect = basegfx::utils::createPolygonFromRect(aRectangle);
    invert(aRect, eFlags);
}

void SkiaSalGraphicsImpl::invert(sal_uInt32 nPoints, const SalPoint* pPointArray, SalInvert eFlags)
{
    basegfx::B2DPolygon aPolygon;
    aPolygon.append(basegfx::B2DPoint(pPointArray[0].mnX, pPointArray[0].mnY), nPoints);
    for (sal_uInt32 i = 1; i < nPoints; ++i)
    {
        aPolygon.setB2DPoint(i, basegfx::B2DPoint(pPointArray[i].mnX, pPointArray[i].mnY));
    }
    aPolygon.setClosed(true);

    invert(aPolygon, eFlags);
}

bool SkiaSalGraphicsImpl::drawEPS(long, long, long, long, void*, sal_uInt32)
{
    // TODO?
    return false;
}

bool SkiaSalGraphicsImpl::drawAlphaBitmap(const SalTwoRect& rPosAry, const SalBitmap& rSourceBitmap,
                                          const SalBitmap& rAlphaBitmap)
{
    assert(dynamic_cast<const SkiaSalBitmap*>(&rSourceBitmap));
    assert(dynamic_cast<const SkiaSalBitmap*>(&rAlphaBitmap));
    sk_sp<SkSurface> tmpSurface = SkiaHelper::createSkSurface(rSourceBitmap.GetSize());
    if (!tmpSurface)
        return false;
    SkCanvas* canvas = tmpSurface->getCanvas();
    SkPaint paint;
    paint.setBlendMode(SkBlendMode::kSrc); // copy as is, including alpha
    canvas->drawImage(static_cast<const SkiaSalBitmap&>(rSourceBitmap).GetSkImage(), 0, 0, &paint);
    paint.setBlendMode(SkBlendMode::kDstOut); // VCL alpha is one-minus-alpha
    canvas->drawImage(static_cast<const SkiaSalBitmap&>(rAlphaBitmap).GetAlphaSkImage(), 0, 0,
                      &paint);
    drawImage(rPosAry, tmpSurface->makeImageSnapshot());
    return true;
}

void SkiaSalGraphicsImpl::drawImage(const SalTwoRect& rPosAry, const sk_sp<SkImage>& aImage,
                                    SkBlendMode eBlendMode)
{
    SkRect aSourceRect
        = SkRect::MakeXYWH(rPosAry.mnSrcX, rPosAry.mnSrcY, rPosAry.mnSrcWidth, rPosAry.mnSrcHeight);
    SkRect aDestinationRect = SkRect::MakeXYWH(rPosAry.mnDestX, rPosAry.mnDestY,
                                               rPosAry.mnDestWidth, rPosAry.mnDestHeight);

    SkPaint aPaint;
    aPaint.setBlendMode(eBlendMode);

    preDraw();
    SAL_INFO("vcl.skia", "drawimage(" << this << "): " << rPosAry << ":" << int(eBlendMode));
    getDrawCanvas()->drawImageRect(aImage, aSourceRect, aDestinationRect, &aPaint);
    if (mXorMode) // limit xor area update
        mXorExtents = aDestinationRect;
    postDraw();
}

void SkiaSalGraphicsImpl::drawBitmap(const SalTwoRect& rPosAry, const SkBitmap& aBitmap,
                                     SkBlendMode eBlendMode)
{
    SkRect aSourceRect
        = SkRect::MakeXYWH(rPosAry.mnSrcX, rPosAry.mnSrcY, rPosAry.mnSrcWidth, rPosAry.mnSrcHeight);
    SkRect aDestinationRect = SkRect::MakeXYWH(rPosAry.mnDestX, rPosAry.mnDestY,
                                               rPosAry.mnDestWidth, rPosAry.mnDestHeight);

    SkPaint aPaint;
    aPaint.setBlendMode(eBlendMode);

    preDraw();
    SAL_INFO("vcl.skia", "drawbitmap(" << this << "): " << rPosAry << ":" << int(eBlendMode));
    getDrawCanvas()->drawBitmapRect(aBitmap, aSourceRect, aDestinationRect, &aPaint);
    if (mXorMode) // limit xor area update
        mXorExtents = aDestinationRect;
    mPendingPixelsToFlush += aBitmap.width() * aBitmap.height();
    postDraw();
}

bool SkiaSalGraphicsImpl::drawTransformedBitmap(const basegfx::B2DPoint& rNull,
                                                const basegfx::B2DPoint& rX,
                                                const basegfx::B2DPoint& rY,
                                                const SalBitmap& rSourceBitmap,
                                                const SalBitmap* pAlphaBitmap)
{
    assert(dynamic_cast<const SkiaSalBitmap*>(&rSourceBitmap));
    assert(!pAlphaBitmap || dynamic_cast<const SkiaSalBitmap*>(pAlphaBitmap));

    const SkiaSalBitmap& rSkiaBitmap = static_cast<const SkiaSalBitmap&>(rSourceBitmap);
    const SkiaSalBitmap* pSkiaAlphaBitmap = static_cast<const SkiaSalBitmap*>(pAlphaBitmap);

    sk_sp<SkSurface> tmpSurface = SkiaHelper::createSkSurface(rSourceBitmap.GetSize());
    if (!tmpSurface)
        return false;

    // Combine bitmap + alpha bitmap into one temporary bitmap with alpha
    SkCanvas* aCanvas = tmpSurface->getCanvas();
    SkPaint aPaint;
    aPaint.setBlendMode(SkBlendMode::kSrc); // copy as is, including alpha
    aCanvas->drawImage(rSkiaBitmap.GetSkImage(), 0, 0, &aPaint);
    if (pSkiaAlphaBitmap != nullptr)
    {
        aPaint.setBlendMode(SkBlendMode::kDstOut); // VCL alpha is one-minus-alpha
        aCanvas->drawImage(pSkiaAlphaBitmap->GetAlphaSkImage(), 0, 0, &aPaint);
    }
    // setup the image transformation
    // using the rNull, rX, rY points as destinations for the (0,0), (0,Width), (Height,0) source points
    const basegfx::B2DVector aXRel = rX - rNull;
    const basegfx::B2DVector aYRel = rY - rNull;

    const Size aSize = rSourceBitmap.GetSize();

    SkMatrix aMatrix;
    aMatrix.set(SkMatrix::kMScaleX, aXRel.getX() / aSize.Width());
    aMatrix.set(SkMatrix::kMSkewY, aXRel.getY() / aSize.Width());
    aMatrix.set(SkMatrix::kMSkewX, aYRel.getX() / aSize.Height());
    aMatrix.set(SkMatrix::kMScaleY, aYRel.getY() / aSize.Height());
    aMatrix.set(SkMatrix::kMTransX, rNull.getX());
    aMatrix.set(SkMatrix::kMTransY, rNull.getY());

    preDraw();
    SAL_INFO("vcl.skia",
             "drawtransformedbitmap(" << this << "): " << rNull << ":" << rX << ":" << rY);
    {
        SkAutoCanvasRestore autoRestore(getDrawCanvas(), true);
        getDrawCanvas()->concat(aMatrix);
        getDrawCanvas()->drawImage(tmpSurface->makeImageSnapshot(), 0, 0);
    }
    postDraw();

    return true;
}

bool SkiaSalGraphicsImpl::drawAlphaRect(long nX, long nY, long nWidth, long nHeight,
                                        sal_uInt8 nTransparency)
{
    privateDrawAlphaRect(nX, nY, nWidth, nHeight, nTransparency / 100.0);
    return true;
}

bool SkiaSalGraphicsImpl::drawGradient(const tools::PolyPolygon&, const Gradient&)
{
    // TODO?
    return false;
}

bool SkiaSalGraphicsImpl::supportsOperation(OutDevSupportType eType) const
{
    switch (eType)
    {
        case OutDevSupportType::B2DDraw:
        case OutDevSupportType::TransparentRect:
            return true;
        default:
            return false;
    }
}

#ifdef DBG_UTIL
void SkiaSalGraphicsImpl::dump(const char* file) const
{
    assert(mSurface.get());
    SkiaHelper::dump(mSurface, file);
}
#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
