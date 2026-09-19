/*
    Copyright 2019 WerWolv
    Copyright 2019 p-sam
    Copyright 2020-2021 natinusala

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <yoga/YGNode.h>

#include <borealis/core/application.hpp>
#include <borealis/core/util.hpp>
#include <borealis/views/image.hpp>

#include "borealis/core/cache_helper.hpp"
#if defined(PS5_NATIVE_GPU)
#include <exception>
#include <limits>
#endif
#include "borealis/core/thread.hpp"

namespace brls
{

static float measureWidth(YGNodeRef node, float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode, float originalWidth, ImageScalingType type)
{
    if (widthMode == YGMeasureModeUndefined)
        return originalWidth;
    else if (widthMode == YGMeasureModeAtMost)
        if (type == ImageScalingType::FIT)
            return originalWidth;
        else
            return std::min(width, originalWidth);
    else if (widthMode == YGMeasureModeExactly)
        return width;
    else
        fatal("Unsupported Image width measure mode: " + std::to_string(widthMode));

    return width;
}

static float measureHeight(YGNodeRef node, float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode, float originalHeight, ImageScalingType type)
{
    if (heightMode == YGMeasureModeUndefined)
        return originalHeight;
    else if (heightMode == YGMeasureModeAtMost)
        if (type == ImageScalingType::FIT)
            return originalHeight;
        else
            return std::min(height, originalHeight);
    else if (heightMode == YGMeasureModeExactly)
        return height;
    else
        fatal("Unsupported Image height measure mode: " + std::to_string(heightMode));

    return height;
}

static YGSize imageMeasureFunc(YGNodeRef node, float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode)
{
    Image* image                 = (Image*)node->getContext();
    int texture                  = image->getTexture();
    float originalWidth          = image->getOriginalImageWidth();
    float originalHeight         = image->getOriginalImageHeight();
    ImageScalingType scalingType = image->getScalingType();

    YGSize size = {
        .width  = width,
        .height = height,
    };

    if (texture == 0)
        return size;

    // Stretched mode: we don't care about the size of the image
    if (scalingType == ImageScalingType::STRETCH)
    {
        return size;
    }
    // Fit scaling mode: scale the view according to image ratio
    else if (scalingType == ImageScalingType::FIT && ntz(height) > 0)
    {
        float imageAspectRatio = originalWidth / originalHeight;

        // Grow height as much as possible then deduce width
        if (heightMode != YGMeasureModeUndefined)
        {
            if (ntz(width) > 0)
            {
                float viewAspectRatio = width / height;
                if (viewAspectRatio > imageAspectRatio)
                {
                    size.height = height;
                    size.width  = height * imageAspectRatio;
                }
                else
                {
                    size.width  = width;
                    size.height = width / imageAspectRatio;
                }
            }
            else
            {
                size.height = measureHeight(node, width, widthMode, height, heightMode, originalHeight, scalingType);
                size.width  = measureWidth(node, width, widthMode, height, heightMode, size.height * imageAspectRatio, scalingType);
            }
        }
        // Grow width as much as possible then deduce height
        else
        {
            size.width  = measureWidth(node, width, widthMode, height, heightMode, originalWidth, scalingType);
            size.height = measureHeight(node, width, widthMode, height, heightMode, size.width / imageAspectRatio, scalingType);
        }
    }
    // Crop (and fallback) method: grow as much as possible in both directions
    else
    {
        size.width  = measureWidth(node, width, widthMode, height, heightMode, originalWidth, scalingType);
        size.height = measureHeight(node, width, widthMode, height, heightMode, originalHeight, scalingType);
    }

    return size;
}

Image::Image()
{
    YGNodeSetMeasureFunc(this->ygNode, imageMeasureFunc);

    // This view uses a custom measure function, so the node type is automatically set to YGNodeTypeText,
    // which causes some deviations in the calculation of YGRoundToPixelGrid.
    // Another solution is to call `defaultConfig->setPointScaleFactor(factor)` in application.cpp.
    // (factor can be 0.0f or a larger value.)
    YGNodeSetNodeType(this->ygNode, YGNodeTypeDefault);

    BRLS_REGISTER_ENUM_XML_ATTRIBUTE(
        "scalingType", ImageScalingType, this->setScalingType,
        {
            { "fit", ImageScalingType::FIT },
            { "fill", ImageScalingType::FILL },
            { "stretch", ImageScalingType::STRETCH },
            { "center", ImageScalingType::CENTER },
        });

    BRLS_REGISTER_ENUM_XML_ATTRIBUTE(
        "imageAlign", ImageAlignment, this->setImageAlign,
        {
            { "top", ImageAlignment::TOP },
            { "right", ImageAlignment::RIGHT },
            { "bottom", ImageAlignment::BOTTOM },
            { "left", ImageAlignment::LEFT },
            { "center", ImageAlignment::CENTER },
        });

    BRLS_REGISTER_ENUM_XML_ATTRIBUTE(
        "interpolation", ImageInterpolation, this->setInterpolation,
        {
            { "linear", ImageInterpolation::LINEAR },
            { "nearest", ImageInterpolation::NEAREST },
        });

    this->registerFilePathXMLAttribute("image", [this](const std::string& value)
        { this->setImageFromFile(value); }

    );

    setClipsToBounds(true);
}

void Image::draw(NVGcontext* vg, float x, float y, float width, float height, Style style, FrameContext* ctx)
{
#if defined(PS5_NATIVE_GPU)
    if (artworkRetryHandler)
    {
        artworkRetry.poll([this] { artworkRetryHandler(this, artworkRetryOwner); });
    }
#endif
    if (this->texture == 0)
        return;

    float coordX = x + this->imageX;
    float coordY = y + this->imageY;

    this->paint.xform[4] = coordX;
    this->paint.xform[5] = coordY;

    nvgBeginPath(vg);
    if (getClipsToBounds())
    {
        nvgRoundedRect(vg, x, y, width, height, getCornerRadius());
    }
    else
    {
        nvgRoundedRect(vg, coordX, coordY, this->imageWidth, this->imageHeight, getCornerRadius());
    }
    nvgFillPaint(vg, a(this->paint));
    nvgFill(vg);
}

void Image::onLayout()
{
    this->invalidateImageBounds();
}

void Image::setImageAlign(ImageAlignment align)
{
    this->align = align;
    this->invalidateImageBounds();
}

void Image::invalidateImageBounds()
{
    if (this->texture == 0)
        return;

    float width  = this->getWidth();
    float height = this->getHeight();

    float viewAspectRatio  = height / width;
    float imageAspectRatio = this->originalImageHeight / this->originalImageWidth;

    switch (this->scalingType)
    {
        case ImageScalingType::FIT:
        {
            if (viewAspectRatio <= imageAspectRatio)
            {
                this->imageHeight = this->getHeight();
                this->imageWidth  = this->imageHeight / imageAspectRatio;
                this->imageX      = (width - this->imageWidth) / 2.0F;
                this->imageY      = 0;
            }
            else
            {
                this->imageWidth  = this->getWidth();
                this->imageHeight = this->imageWidth * imageAspectRatio;
                this->imageY      = (height - this->imageHeight) / 2.0F;
                this->imageX      = 0;
            }
            break;
        }
        case ImageScalingType::FILL:
        {
            if (viewAspectRatio >= imageAspectRatio)
            {
                this->imageHeight = this->getHeight();
                this->imageWidth  = this->imageHeight / imageAspectRatio;
                this->imageX      = (width - this->imageWidth) / 2.0F;
                this->imageY      = 0;
            }
            else
            {
                this->imageWidth  = this->getWidth();
                this->imageHeight = this->imageWidth * imageAspectRatio;
                this->imageY      = (height - this->imageHeight) / 2.0F;
                this->imageX      = 0;
            }
            break;
        }
        case ImageScalingType::STRETCH:
            this->imageX      = 0;
            this->imageY      = 0;
            this->imageWidth  = this->getWidth();
            this->imageHeight = this->getHeight();
            break;
        case ImageScalingType::CENTER:
            this->imageHeight = this->originalImageHeight;
            this->imageWidth  = this->originalImageWidth;
            this->imageX      = (width - this->imageWidth) / 2.0F;
            this->imageY      = (height - this->imageHeight) / 2.0F;
            break;
        default:
            fatal("Unimplemented Image scaling type");
    }

    // Create the paint - actual X and Y positions are updated every frame in draw() to apply translation (scrolling...)
    NVGcontext* vg = Application::getNVGContext();
    this->paint    = nvgImagePattern(vg, 0, 0, this->imageWidth, this->imageHeight, 0, this->texture, 1.0f);
}

size_t Image::checkCache(const std::string& path)
{
#if !defined(PS5_NATIVE_GPU)
    if (this->texture > 0)
    {
        brls::TextureCache::instance().removeCache(this->texture);
        brls::Logger::verbose("cache remove: {} {}", path, this->texture);
    }
#endif

    int tex = brls::TextureCache::instance().getCache(path);
    if (tex > 0)
    {
#if defined(PS5_NATIVE_GPU)
        this->setImageFromCache(tex);
#else
        brls::Logger::verbose("cache hit: {} {}", path, tex);
        this->innerSetImage(tex);
#endif
        return tex;
    }

    return 0;
}

void Image::setImageFromRes(const std::string& path)
{
#if defined(PS5_NATIVE_GPU)
    artworkRetry.cancel();
    if (TextureCache::instance().isClosing())
        return;
#endif
    // Let TextureCache to manage when to delete texture
    this->setFreeTexture(false);

#ifdef USE_LIBROMFS
#if defined(PS5_NATIVE_GPU)
    // Prepare the key before upload so key allocation cannot strand a texture.
    const std::string key = "@res/" + path;
    if (checkCache(key) > 0)
        return;
    const romfs::Resource* image = nullptr;
    try
    {
        image = &romfs::get(path);
    }
    catch (const std::exception&)
    {
        Logger::error("Cannot load native image resource");
        return;
    }
    if (image->data() == nullptr || image->size() == 0 || image->size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        return;
    int tex = nvgCreateImageMem(Application::getNVGContext(), 0,
        const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(image->data())), static_cast<int>(image->size()));
    if (tex <= 0)
        return;
    if (!TextureCache::instance().tryAddCache(key, tex))
    {
        nvgDeleteImage(Application::getNVGContext(), tex);
        return;
    }
    this->setImageFromCache(tex);
#else
    if (checkCache("@res/" + path) > 0)
        return;
    auto image = romfs::get(path);
    this->setImageFromMem((unsigned char*)image.data(), (int)image.size());
    TextureCache::instance().addCache("@res/" + path, this->texture);
#endif
#else
#ifdef PS5_NATIVE_GPU
    this->setImageFromFile(resourceBase() + path);
#else
    this->setImageFromFile(std::string(BRLS_RESOURCES) + path);
#endif
#endif
}

void Image::setInterpolation(ImageInterpolation interpolation)
{
    this->interpolation = interpolation;
}

int Image::getImageFlags()
{
    if (this->interpolation == ImageInterpolation::NEAREST)
        return NVG_IMAGE_NEAREST;

    return 0;
}

void Image::setImageFromFile(const std::string& path)
{
#if defined(PS5_NATIVE_GPU)
    artworkRetry.cancel();
    if (TextureCache::instance().isClosing())
        return;
#endif
    // Let TextureCache to manage when to delete texture
    this->setFreeTexture(false);

#ifdef USE_LIBROMFS
    if (path.rfind("@res/", 0) == 0)
        return this->setImageFromRes(path.substr(5));
#endif
    if (checkCache(path) > 0)
        return;

    // Load texture
    int tex = nvgCreateImage(Application::getNVGContext(), path.c_str(), this->getImageFlags());
#if defined(PS5_NATIVE_GPU)
    if (tex <= 0)
        return;
    if (!TextureCache::instance().tryAddCache(path, tex))
    {
        nvgDeleteImage(Application::getNVGContext(), tex);
        return;
    }
    this->setImageFromCache(tex);
#else
    innerSetImage(tex);

    // Save cache
    TextureCache::instance().addCache(path, tex);
#endif
}

void Image::setImageFromMem(const unsigned char* data, int size)
{
#if defined(PS5_NATIVE_GPU)
    artworkRetry.cancel();
    if (data == nullptr || size <= 0 || TextureCache::instance().isClosing())
        return;
#endif
    NVGcontext* vg = Application::getNVGContext();

    // Load texture
#if defined(PS5_NATIVE_GPU)
    // This method creates the allocation itself; raw borrowed-handle policy
    // must not turn a newly decoded image into an unowned allocation.
    replaceNativeTexture(nvgCreateImageMem(vg, 0, const_cast<unsigned char*>(data), size), NativeTextureOwnership::OWNED);
#else
    innerSetImage(nvgCreateImageMem(vg, 0, const_cast<unsigned char*>(data), size));
#endif
}

void Image::setImageAsync(std::function<void(std::function<void(const std::string&, size_t length)>)> cb)
{
#if defined(PS5_NATIVE_GPU)
    artworkRetry.cancel();
#endif
    ASYNC_RETAIN
    cb([ASYNC_TOKEN](const std::string& data, size_t length)
        { brls::sync([ASYNC_TOKEN, data, length]()
              {
            ASYNC_RELEASE
            if(length == 0)
                return;
            this->setImageFromMem((unsigned char *) data.c_str(),(int) length); }); });
}

void Image::innerSetImage(int tex)
{
#if defined(PS5_NATIVE_GPU)
    artworkRetry.cancel();
    // Reassigning the current raw handle is idempotent. In particular, a
    // borrowed alias cannot replace the cached lease that keeps it alive.
    if (tex > 0 && tex == this->texture)
        return;
    replaceNativeTexture(tex, this->freeTexture ? NativeTextureOwnership::OWNED : NativeTextureOwnership::BORROWED);
#else
    if (tex == 0)
    {
        Logger::error("Cannot set texture: 0");
        return;
    }

    NVGcontext* vg = Application::getNVGContext();

    // Free the old texture if necessary
    if (this->texture != 0 && this->freeTexture)
        nvgDeleteImage(vg, this->texture);

    // Set the new texture
    this->texture = tex;

    int width, height;
    nvgImageSize(vg, this->texture, &width, &height);
    this->originalImageWidth  = (float)width;
    this->originalImageHeight = (float)height;

    this->invalidate();
#endif
}

#if defined(PS5_NATIVE_GPU)
void Image::setImageFromCache(int tex)
{
    artworkRetry.cancel();
    if (TextureCache::instance().isClosing())
        return;
    replaceNativeTexture(tex, NativeTextureOwnership::CACHED);
}

void Image::replaceNativeTexture(int tex, NativeTextureOwnership ownership)
{
    if (tex <= 0)
    {
        Logger::error("Cannot set native image: invalid texture");
        return;
    }

    if (tex == this->texture && ownership == NativeTextureOwnership::CACHED)
    {
        // Keep the already validated image. Transfer direct/borrowed ownership
        // to the incoming cache lease, or balance its extra acquire if cached.
        if (this->nativeTextureOwnership == NativeTextureOwnership::CACHED)
            TextureCache::instance().removeCache(tex);
        this->nativeTextureOwnership = NativeTextureOwnership::CACHED;
        return;
    }

    auto vg = Application::getNVGContext();
    int width = 0, height = 0;
    nvgImageSize(vg, tex, &width, &height);
    if (width <= 0 || height <= 0)
    {
        // Consume the rejected incoming ownership, retaining the old image.
        if (ownership == NativeTextureOwnership::CACHED)
        {
            // A retained idle entry must not turn every later lookup into
            // another invalid hit. Unkey it before releasing this acquire.
            TextureCache::instance().markDirty(tex);
            TextureCache::instance().removeCache(tex);
        }
        else if (ownership == NativeTextureOwnership::OWNED && tex != this->texture)
            nvgDeleteImage(vg, tex);
        Logger::error("Cannot set native image: invalid dimensions");
        return;
    }

    const int previous = this->texture;
    const auto previousOwnership = this->nativeTextureOwnership;
    this->texture = tex;
    this->nativeTextureOwnership = ownership;
    this->originalImageWidth = static_cast<float>(width);
    this->originalImageHeight = static_cast<float>(height);

    if (previous > 0)
    {
        if (previousOwnership == NativeTextureOwnership::CACHED)
            TextureCache::instance().removeCache(previous);
        else if (previousOwnership == NativeTextureOwnership::OWNED && previous != tex)
            nvgDeleteImage(vg, previous);
    }
    this->invalidate();
}

void Image::releaseNativeTexture()
{
    const int previous = this->texture;
    const auto ownership = this->nativeTextureOwnership;
    this->texture = 0;
    this->nativeTextureOwnership = NativeTextureOwnership::BORROWED;
    this->originalImageWidth = this->originalImageHeight = 0;
    if (previous > 0)
    {
        if (ownership == NativeTextureOwnership::CACHED)
            TextureCache::instance().removeCache(previous);
        else if (ownership == NativeTextureOwnership::OWNED)
            nvgDeleteImage(Application::getNVGContext(), previous);
    }
}
#endif

void Image::clear()
{
#if defined(PS5_NATIVE_GPU)
    artworkRetry.cancel();
    releaseNativeTexture();
#else
    if (this->texture == 0)
        return;

    if (this->freeTexture)
        nvgDeleteImage(Application::getNVGContext(), this->texture);

    this->texture = 0;
#endif
}

void Image::setScalingType(ImageScalingType scalingType)
{
    this->scalingType = scalingType;

    this->invalidate();
}

ImageScalingType Image::getScalingType()
{
    return this->scalingType;
}

float Image::getOriginalImageWidth()
{
    return this->originalImageWidth;
}

int Image::getTexture()
{
    return this->texture;
}

void Image::setFreeTexture(bool value)
{
    this->freeTexture = value;
}

bool Image::getFreeTexture()
{
    return this->freeTexture;
}

float Image::getOriginalImageHeight()
{
    return this->originalImageHeight;
}

Image::~Image()
{
#if defined(PS5_NATIVE_GPU)
    setArtworkRetryHandler(nullptr, nullptr);
    releaseNativeTexture();
#else
    if (this->freeTexture && this->texture != 0)
        nvgDeleteImage(Application::getNVGContext(), this->texture);
    else
        TextureCache::instance().removeCache(this->texture);
#endif
}

View* Image::create()
{
    return new Image();
}

} // namespace brls
