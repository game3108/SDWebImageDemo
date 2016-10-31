##前言
CSDN地址：http://blog.csdn.net/game3108/article/details/52638886
本文的中文注释代码demo更新在我的[github](https://github.com/game3108/SDWebImageDemo)上。

[上篇文章](http://www.jianshu.com/p/685e9b7ec4b2)讲解的了SDWebImage的Download部分，这篇讲讲一下Utils部分。

##Utils
Utils主要包含以下3个类：
* SDWebImageManager
核心的下载控制类
* SDWebImageDecoder
图片解码类
* SDWebImagePrefetcher
图片预下载类

以下将分别介绍3个类的源代码。

##SDWebImageDecoder
SDWebImageDecoder内容就是UIImage的一个Category
定义如下：
```
@interface UIImage (ForceDecode)
//解码图片
+ (UIImage *)decodedImageWithImage:(UIImage *)image;
@end
```
实现如下：
```
+ (UIImage *)decodedImageWithImage:(UIImage *)image {
    // while downloading huge amount of images
    // autorelease the bitmap context
    // and all vars to help system to free memory
    // when there are memory warning.
    // on iOS7, do not forget to call
    // [[SDImageCache sharedImageCache] clearMemory];
    // 当下载大量图片的时候，自动释放bitmap context和所有变量去帮助节约内存
    // 在ios7上别忘记调用 [[SDImageCache sharedImageCache] clearMemory];
    if (image == nil) { // Prevent "CGBitmapContextCreateImage: invalid context 0x0" error
        return nil;
    }
    
    @autoreleasepool{
        // do not decode animated images
        // 不去解码gif图片
        if (image.images != nil) {
            return image;
        }
        
        CGImageRef imageRef = image.CGImage;
        
        CGImageAlphaInfo alpha = CGImageGetAlphaInfo(imageRef);
        //获取任何的alpha轨道
        BOOL anyAlpha = (alpha == kCGImageAlphaFirst ||
                         alpha == kCGImageAlphaLast ||
                         alpha == kCGImageAlphaPremultipliedFirst ||
                         alpha == kCGImageAlphaPremultipliedLast);
        //有alpha信息的图片，直接返回
        if (anyAlpha) {
            return image;
        }
        
        // current
        //表示需要使用的色彩标准（为创建CGColor做准备）
        //例如RBG：CGColorSpaceCreateDeviceRGB
        CGColorSpaceModel imageColorSpaceModel = CGColorSpaceGetModel(CGImageGetColorSpace(imageRef));
        CGColorSpaceRef colorspaceRef = CGImageGetColorSpace(imageRef);
        
        //是否不支持这些标准
        BOOL unsupportedColorSpace = (imageColorSpaceModel == kCGColorSpaceModelUnknown ||
                                      imageColorSpaceModel == kCGColorSpaceModelMonochrome ||
                                      imageColorSpaceModel == kCGColorSpaceModelCMYK ||
                                      imageColorSpaceModel == kCGColorSpaceModelIndexed);
        //如果不支持说明是RGB的标准
        if (unsupportedColorSpace) {
            colorspaceRef = CGColorSpaceCreateDeviceRGB();
        }
        
        //获取图片高和宽
        size_t width = CGImageGetWidth(imageRef);
        size_t height = CGImageGetHeight(imageRef);
        //每一个pixel是4个byte
        NSUInteger bytesPerPixel = 4;
        //每一行的byte大小
        NSUInteger bytesPerRow = bytesPerPixel * width;
        //1Byte=8bit
        NSUInteger bitsPerComponent = 8;


        // kCGImageAlphaNone is not supported in CGBitmapContextCreate.
        // Since the original image here has no alpha info, use kCGImageAlphaNoneSkipLast
        // to create bitmap graphics contexts without alpha info.
        // kCGImageAlphaNone无法使用CGBitmapContextCreate.
        // 既然原始图片这边没有alpha信息，就使用kCGImageAlphaNoneSkipLast
        // 去创造没有alpha信息的bitmap的图片讯息
        CGContextRef context = CGBitmapContextCreate(NULL,
                                                     width,
                                                     height,
                                                     bitsPerComponent,
                                                     bytesPerRow,
                                                     colorspaceRef,
                                                     kCGBitmapByteOrderDefault|kCGImageAlphaNoneSkipLast);
        
        // Draw the image into the context and retrieve the new bitmap image without alpha
        // 将图片描绘进 图片上下文 去 取回新的没有alpha的bitmap图片
        CGContextDrawImage(context, CGRectMake(0, 0, width, height), imageRef);
        CGImageRef imageRefWithoutAlpha = CGBitmapContextCreateImage(context);
        //适应屏幕和方向
        UIImage *imageWithoutAlpha = [UIImage imageWithCGImage:imageRefWithoutAlpha
                                                         scale:image.scale
                                                   orientation:image.imageOrientation];
        
        if (unsupportedColorSpace) {
            CGColorSpaceRelease(colorspaceRef);
        }
        
        CGContextRelease(context);
        CGImageRelease(imageRefWithoutAlpha);
        
        return imageWithoutAlpha;
    }
}
```
这边解码图片的做法和download完成处理中的类似，两边可以一起验证一下。
这边解码图片主要原因就是图片的加载是lazy加载，在真正显示的时候才进行加载，这边先解码一次就会直接把图片加载完成。

##SDWebImageManager
SDWebImageManager是核心的下载控制类方法，它内部封装了``SDImageCache``与``SDWebImageDownloader``
主要包含以下几块：
* 定义与初始化相关
* 存储与判断的一些封装方法
* 下载图片核心方法

还有一些已经被废弃的实现，为了支持上的需要，也没删掉，这里也就不多说明了

###定义与初始化相关
SDWebImageManager.h中不仅包含SDWebImageManager类的声明，还包含了相关的delegate方法``@protocol SDWebImageManagerDelegate <NSObject>``还有下载设置enum与相关block的声明。
定义如下：
```
//下载设置
typedef NS_OPTIONS(NSUInteger, SDWebImageOptions) {
    //默认，当一个url下载失败，这个url会到黑名单中不再尝试下载
    //这个设置禁止这个黑名单策略
    SDWebImageRetryFailed = 1 << 0,
    
    //默认，图片从ui交互的时候就开始下载。这个设置是的下载延迟到uiscrollview放手拖动的的时候开始下载
    SDWebImageLowPriority = 1 << 1,

    //只存在内存中
    SDWebImageCacheMemoryOnly = 1 << 2,

    //这个设置允许进度下载。默认情况下，图片只在下载完成展示
    SDWebImageProgressiveDownload = 1 << 3,

    //即使图片被缓存了，需要http的缓存策略控制并且在需要刷新的时候刷新
    //硬盘缓存会通过NSURLCache缓存而不是SDWebImage会造成轻微的性能下降
    //这个设置可以帮助处理图片在同一个url下变化的问题。比如facebook的图片api：profile pics
    //如果一个缓存图片已经更新，完成回调会调用一次完成的图片
    SDWebImageRefreshCached = 1 << 4,

    //iOS4以上，持续下载图片当app进入后台。这个是通过请求系统在后台的额外时间让请求完成
    //如果后台需要操作则会被取消
    SDWebImageContinueInBackground = 1 << 5,

    //处理设置在NSHTTPCookieStore的cookie
    SDWebImageHandleCookies = 1 << 6,

    //允许访问不信任的SSL证书
    //用来测试的目的，小心在生产环境中使用
    SDWebImageAllowInvalidSSLCertificates = 1 << 7,

    //默认，图片会按照顺序下载，这个会提高它的顺序
    SDWebImageHighPriority = 1 << 8,
    
    //默认占位图片会在图片加载过程中使用，这个设置可以延迟加载占位图片直到图片加载完成之后
    SDWebImageDelayPlaceholder = 1 << 9,

    //我们不会去调transformDownloadedImage delegate方法在动画图片上，因为这个会撕裂图片
    //用这个设置去调用它
    SDWebImageTransformAnimatedImage = 1 << 10,

    //默认，图片会在图片下载完成后加到imageview上。单在有些情况，我们想设置图片前进行一些设置（比如加上筛选或者淡入淡出的动画）
    //用这个设置去手动在下载完成后设置
    SDWebImageAvoidAutoSetImage = 1 << 11
};
/**
 下载完成block

 @param image     图片
 @param error     错误
 @param cacheType 下载类型
 @param imageURL  图片url
 */
typedef void(^SDWebImageCompletionBlock)(UIImage *image, NSError *error, SDImageCacheType cacheType, NSURL *imageURL);
/**
 下载完成block

 @param image     图片
 @param error     错误
 @param cacheType 下载类型
 @param finished  是否完成
 @param imageURL  图片url
 */
typedef void(^SDWebImageCompletionWithFinishedBlock)(UIImage *image, NSError *error, SDImageCacheType cacheType, BOOL finished, NSURL *imageURL);
/**
 筛选缓存key的block

 @param url 图片url

 @return 筛选的key
 */
typedef NSString *(^SDWebImageCacheKeyFilterBlock)(NSURL *url);
@class SDWebImageManager;

@protocol SDWebImageManagerDelegate <NSObject>

@optional

/**
 控制那个图片在没有缓存的时候下载

 @param imageManager 当前的`SDWebImageMananger`
 @param imageURL     图片下载url

 @return 返回NO去防止下载图片在cache上没有找到。没有实现默认YES
 */
- (BOOL)imageManager:(SDWebImageManager *)imageManager shouldDownloadImageForURL:(NSURL *)imageURL;

/**
 允许去在图片下载完成后，先于缓存到硬盘和内存前进行转换
 注意：这个方法在一个global线程上被调用，为了不阻塞主线程

 @param imageManager 当前的`SDWebImageManager`
 @param image        需要变化的图片
 @param imageURL     图片的url

 @return 已经变化了的图片
 */
- (UIImage *)imageManager:(SDWebImageManager *)imageManager transformDownloadedImage:(UIImage *)image withURL:(NSURL *)imageURL;
@end

@interface SDWebImageManager : NSObject

@property (weak, nonatomic) id <SDWebImageManagerDelegate> delegate;
//缓存，这边只读，在.m的extension中，可以重写为readwrite
@property (strong, nonatomic, readonly) SDImageCache *imageCache;
//下载器
@property (strong, nonatomic, readonly) SDWebImageDownloader *imageDownloader;
/**
 这个缓存筛选是在每次SDWebImageManager需要变化一个url到一个缓存key的时候调用，可以用来移除动态的image图片
 下面的例子设置了一个筛选器在application delegate可以移除任何的查询字符串从url，在使用它当做缓存key之前

 * @code
 
 [[SDWebImageManager sharedManager] setCacheKeyFilter:^(NSURL *url) {
 url = [[NSURL alloc] initWithScheme:url.scheme host:url.host path:url.path];
 return [url absoluteString];
 }];
 
 * @endcode
 */
@property (nonatomic, copy) SDWebImageCacheKeyFilterBlock cacheKeyFilter;
```
在SDWebImageManager.m中，定义了一个NSOperation的封装``SDWebImageCombinedOperation``，以及失败url的存储和正在运行operation的存储。
实现如下：
```
//nsoperation的一层封装,实现SDWebImageOperation协议
@interface SDWebImageCombinedOperation : NSObject <SDWebImageOperation>

@property (assign, nonatomic, getter = isCancelled) BOOL cancelled;
@property (copy, nonatomic) SDWebImageNoParamsBlock cancelBlock;
@property (strong, nonatomic) NSOperation *cacheOperation;

@end

@interface SDWebImageManager ()
//重新，可以在内部读与写
@property (strong, nonatomic, readwrite) SDImageCache *imageCache;
@property (strong, nonatomic, readwrite) SDWebImageDownloader *imageDownloader;
//失败url的存储
@property (strong, nonatomic) NSMutableSet *failedURLs;
//正在运行的operation组合
@property (strong, nonatomic) NSMutableArray *runningOperations;

+ (id)sharedManager {
    static dispatch_once_t once;
    static id instance;
    dispatch_once(&once, ^{
        instance = [self new];
    });
    return instance;
}

//初始化默认的cache和downloader
- (instancetype)init {
    SDImageCache *cache = [SDImageCache sharedImageCache];
    SDWebImageDownloader *downloader = [SDWebImageDownloader sharedDownloader];
    return [self initWithCache:cache downloader:downloader];
}

- (instancetype)initWithCache:(SDImageCache *)cache downloader:(SDWebImageDownloader *)downloader {
    if ((self = [super init])) {
        _imageCache = cache;
        _imageDownloader = downloader;
        _failedURLs = [NSMutableSet new];
        _runningOperations = [NSMutableArray new];
    }
    return self;
}
@end

@implementation SDWebImageCombinedOperation

- (void)setCancelBlock:(SDWebImageNoParamsBlock)cancelBlock {
    // check if the operation is already cancelled, then we just call the cancelBlock
    //检查是否操作已经cancel了，才能去调用cancelblock
    if (self.isCancelled) {
        if (cancelBlock) {
            cancelBlock();
        }
        //别忘记设置为nil，否则会crash
        _cancelBlock = nil; // don't forget to nil the cancelBlock, otherwise we will get crashes
    } else {
        _cancelBlock = [cancelBlock copy];
    }
}

- (void)cancel {
    //设置取消标志
    self.cancelled = YES;
    //取消存储的operation草走
    if (self.cacheOperation) {
        [self.cacheOperation cancel];
        self.cacheOperation = nil;
    }
    //调用cancelblock
    if (self.cancelBlock) {
        self.cancelBlock();
        
        // TODO: this is a temporary fix to #809.
        // Until we can figure the exact cause of the crash, going with the ivar instead of the setter
//        self.cancelBlock = nil;
        _cancelBlock = nil;
    }
}

@end
```

###存储与判断的一些封装方法
存储与判断的封装就是一些``SDImageCache``的一些查询方法的封装与取消等操作的方法。
相关方法声明如下：
```
/**
 通过url保存图片

 @param image 需要缓存的图片
 @param url   图片url
 */
- (void)saveImageToCache:(UIImage *)image forURL:(NSURL *)url;

/**
 取消所有操作
 */
- (void)cancelAll;

//判断是否有操作还在运行
- (BOOL)isRunning;

/**
 判断是否有图片已经被缓存

 @param url 图片url

 @return 是否图片被缓存
 */
- (BOOL)cachedImageExistsForURL:(NSURL *)url;

/**
 判断是否图片只在在硬盘缓存

 @param url 图片url

 @return 是否图片只在硬盘缓存
 */
- (BOOL)diskImageExistsForURL:(NSURL *)url;

/**
 异步判断是否图片已经被缓存

 @param url             图片url
 @param completionBlock 完成block
 
 @note  完成block一直在主线程调用
 */
- (void)cachedImageExistsForURL:(NSURL *)url
                     completion:(SDWebImageCheckCacheCompletionBlock)completionBlock;

/**
 异步判断是否只在硬盘缓存

 @param url             图片url
 @param completionBlock 完成block
 */
- (void)diskImageExistsForURL:(NSURL *)url
                   completion:(SDWebImageCheckCacheCompletionBlock)completionBlock;

//返回一个url的缓存key
- (NSString *)cacheKeyForURL:(NSURL *)url;
```
方法实现如下：
```
- (NSString *)cacheKeyForURL:(NSURL *)url {
    if (!url) {
        return @"";
    }
    
    //有filter走filter
    if (self.cacheKeyFilter) {
        return self.cacheKeyFilter(url);
    } else {
        //默认是url的完全string
        return [url absoluteString];
    }
}

- (BOOL)cachedImageExistsForURL:(NSURL *)url {
    //获得url的cache key
    NSString *key = [self cacheKeyForURL:url];
    //如果在memory中，直接返回yes
    if ([self.imageCache imageFromMemoryCacheForKey:key] != nil) return YES;
    //否则返回是否在disk中的结果
    return [self.imageCache diskImageExistsWithKey:key];
}

- (BOOL)diskImageExistsForURL:(NSURL *)url {
    //获得url的cache key
    NSString *key = [self cacheKeyForURL:url];
    //返回disk中的结果
    return [self.imageCache diskImageExistsWithKey:key];
}

- (void)cachedImageExistsForURL:(NSURL *)url
                     completion:(SDWebImageCheckCacheCompletionBlock)completionBlock {
    //获得url的cache key
    NSString *key = [self cacheKeyForURL:url];
    
    //判断是否在memory中
    BOOL isInMemoryCache = ([self.imageCache imageFromMemoryCacheForKey:key] != nil);
    
    //在memory中直接回调block
    if (isInMemoryCache) {
        // making sure we call the completion block on the main queue
        dispatch_async(dispatch_get_main_queue(), ^{
            if (completionBlock) {
                completionBlock(YES);
            }
        });
        return;
    }
    
    //否则调用disk的查询和回调
    [self.imageCache diskImageExistsWithKey:key completion:^(BOOL isInDiskCache) {
        // the completion block of checkDiskCacheForImageWithKey:completion: is always called on the main queue, no need to further dispatch
        if (completionBlock) {
            completionBlock(isInDiskCache);
        }
    }];
}

- (void)diskImageExistsForURL:(NSURL *)url
                   completion:(SDWebImageCheckCacheCompletionBlock)completionBlock {
    NSString *key = [self cacheKeyForURL:url];
    
    //直接调用disk的查询和回调
    [self.imageCache diskImageExistsWithKey:key completion:^(BOOL isInDiskCache) {
        // the completion block of checkDiskCacheForImageWithKey:completion: is always called on the main queue, no need to further dispatch
        if (completionBlock) {
            completionBlock(isInDiskCache);
        }
    }];
}

- (void)saveImageToCache:(UIImage *)image forURL:(NSURL *)url {
    if (image && url) {
        NSString *key = [self cacheKeyForURL:url];
        [self.imageCache storeImage:image forKey:key toDisk:YES];
    }
}

- (void)cancelAll {
    //获取所有存储的正在运行中的operation，然后全部取消并删除
    @synchronized (self.runningOperations) {
        NSArray *copiedOperations = [self.runningOperations copy];
        [copiedOperations makeObjectsPerformSelector:@selector(cancel)];
        [self.runningOperations removeObjectsInArray:copiedOperations];
    }
}

- (BOOL)isRunning {
    //判断是否有正在运行的operation
    BOOL isRunning = NO;
    @synchronized(self.runningOperations) {
        isRunning = (self.runningOperations.count > 0);
    }
    return isRunning;
}
```
###下载图片核心方法
下载图片的方法定义如下：
```
/**
 通过url下载图片如果缓存不存在

 @param url            图片url
 @param options        下载设置
 @param progressBlock  进度block
 @param completedBlock 完成block

 @return 返回SDWebImageDownloaderOperation的实例
 */
- (id <SDWebImageOperation>)downloadImageWithURL:(NSURL *)url
                                         options:(SDWebImageOptions)options
                                        progress:(SDWebImageDownloaderProgressBlock)progressBlock
                                       completed:(SDWebImageCompletionWithFinishedBlock)completedBlock;
```
实现如下：
```
- (id <SDWebImageOperation>)downloadImageWithURL:(NSURL *)url
                                         options:(SDWebImageOptions)options
                                        progress:(SDWebImageDownloaderProgressBlock)progressBlock
                                       completed:(SDWebImageCompletionWithFinishedBlock)completedBlock {
    // Invoking this method without a completedBlock is pointless
    //没有完成block，则报错，应该用`[SDWebImagePrefetcher prefetchURLs]`替代
    NSAssert(completedBlock != nil, @"If you mean to prefetch the image, use -[SDWebImagePrefetcher prefetchURLs] instead");

    // Very common mistake is to send the URL using NSString object instead of NSURL. For some strange reason, XCode won't
    // throw any warning for this type mismatch. Here we failsafe this error by allowing URLs to be passed as NSString.
    
    //很常见的错误是把nsstring对象代替nsurl对象传过来。单因为一些奇怪的原因，xcode不会在这种错误上报warning，所以这里我们只能保护一下这个错误允许url当nsstring传过来
    if ([url isKindOfClass:NSString.class]) {
        url = [NSURL URLWithString:(NSString *)url];
    }

    // Prevents app crashing on argument type error like sending NSNull instead of NSURL
    //防止app的崩溃因为参数类型错误，比如传输NSNull
    if (![url isKindOfClass:NSURL.class]) {
        url = nil;
    }

    //创建operation的封装对象
    __block SDWebImageCombinedOperation *operation = [SDWebImageCombinedOperation new];
    __weak SDWebImageCombinedOperation *weakOperation = operation;

    BOOL isFailedUrl = NO;
    //判断是否是已经失败过的url
    @synchronized (self.failedURLs) {
        isFailedUrl = [self.failedURLs containsObject:url];
    }
    
    //如果url长度为0或者说设置不允许失败过的url重试，则调用完成block返回错误
    if (url.absoluteString.length == 0 || (!(options & SDWebImageRetryFailed) && isFailedUrl)) {
        dispatch_main_sync_safe(^{
            NSError *error = [NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorFileDoesNotExist userInfo:nil];
            completedBlock(nil, error, SDImageCacheTypeNone, YES, url);
        });
        return operation;
    }

    //将operation存储
    @synchronized (self.runningOperations) {
        [self.runningOperations addObject:operation];
    }
    //获得url的key
    NSString *key = [self cacheKeyForURL:url];

    //设置nsoperation为先从cache中异步获取
    operation.cacheOperation = [self.imageCache queryDiskCacheForKey:key done:^(UIImage *image, SDImageCacheType cacheType) {
        //如果operation已经被取消，则从存储中删除直接返回
        if (operation.isCancelled) {
            @synchronized (self.runningOperations) {
                [self.runningOperations removeObject:operation];
            }

            return;
        }
        
        //(没有图片 || 需要更新缓存) && (不存在`imageManager:shouldDownloadImageForURL:`方法) || (｀imageManager:shouldDownloadImageForURL:｀返回YES)
        if ((!image || options & SDWebImageRefreshCached) && (![self.delegate respondsToSelector:@selector(imageManager:shouldDownloadImageForURL:)] || [self.delegate imageManager:self shouldDownloadImageForURL:url])) {
            //如果有图片同时需要更新缓存,先返回一次之前缓存的图片
            if (image && options & SDWebImageRefreshCached) {
                dispatch_main_sync_safe(^{
                    // If image was found in the cache but SDWebImageRefreshCached is provided, notify about the cached image
                    // AND try to re-download it in order to let a chance to NSURLCache to refresh it from server.
                    //如果image在cache中，但是SDWebImageRefreshCached被设置。需要注意缓存的图片需要重新下载去似的NSURLCache去从服务器更新它们
                    completedBlock(image, nil, cacheType, YES, url);
                });
            }

            // download if no image or requested to refresh anyway, and download allowed by delegate
            //在没有图片或者一定要下载的时候下载图片
            SDWebImageDownloaderOptions downloaderOptions = 0;
            if (options & SDWebImageLowPriority) downloaderOptions |= SDWebImageDownloaderLowPriority;
            if (options & SDWebImageProgressiveDownload) downloaderOptions |= SDWebImageDownloaderProgressiveDownload;
            if (options & SDWebImageRefreshCached) downloaderOptions |= SDWebImageDownloaderUseNSURLCache;
            if (options & SDWebImageContinueInBackground) downloaderOptions |= SDWebImageDownloaderContinueInBackground;
            if (options & SDWebImageHandleCookies) downloaderOptions |= SDWebImageDownloaderHandleCookies;
            if (options & SDWebImageAllowInvalidSSLCertificates) downloaderOptions |= SDWebImageDownloaderAllowInvalidSSLCertificates;
            if (options & SDWebImageHighPriority) downloaderOptions |= SDWebImageDownloaderHighPriority;
            
            //如果是有图片并且需要刷新缓存的情况
            if (image && options & SDWebImageRefreshCached) {
                // force progressive off if image already cached but forced refreshing
                //强制关闭进度展示
                downloaderOptions &= ~SDWebImageDownloaderProgressiveDownload;
                // ignore image read from NSURLCache if image if cached but force refreshing
                //忽略NSURLCache的缓存
                downloaderOptions |= SDWebImageDownloaderIgnoreCachedResponse;
            }
            //构造下载operation去下载图片
            id <SDWebImageOperation> subOperation = [self.imageDownloader downloadImageWithURL:url options:downloaderOptions progress:progressBlock completed:^(UIImage *downloadedImage, NSData *data, NSError *error, BOOL finished) {
                __strong __typeof(weakOperation) strongOperation = weakOperation;
                //如果取消更新，这边不需要去调用complete block，因为上边已经调过了
                if (!strongOperation || strongOperation.isCancelled) {
                    // Do nothing if the operation was cancelled
                    // See #699 for more details
                    // if we would call the completedBlock, there could be a race condition between this block and another completedBlock for the same object, so if this one is called second, we will overwrite the new data
                }
                //如果存在错误
                else if (error) {
                    dispatch_main_sync_safe(^{
                        if (strongOperation && !strongOperation.isCancelled) {
                            completedBlock(nil, error, SDImageCacheTypeNone, finished, url);
                        }
                    });
                    //如果error不是以下错误，则将url存入失败url的列表
                    if (   error.code != NSURLErrorNotConnectedToInternet
                        && error.code != NSURLErrorCancelled
                        && error.code != NSURLErrorTimedOut
                        && error.code != NSURLErrorInternationalRoamingOff
                        && error.code != NSURLErrorDataNotAllowed
                        && error.code != NSURLErrorCannotFindHost
                        && error.code != NSURLErrorCannotConnectToHost) {
                        @synchronized (self.failedURLs) {
                            [self.failedURLs addObject:url];
                        }
                    }
                }
                else {
                    //如果允许重新下载，则将url从失败url列表中删除
                    if ((options & SDWebImageRetryFailed)) {
                        @synchronized (self.failedURLs) {
                            [self.failedURLs removeObject:url];
                        }
                    }
                    
                    //是否需要在硬盘缓存
                    BOOL cacheOnDisk = !(options & SDWebImageCacheMemoryOnly);
                    
                    //图片更新击中了NSURLCache的缓存，不用掉完成block
                    if (options & SDWebImageRefreshCached && image && !downloadedImage) {
                        // Image refresh hit the NSURLCache cache, do not call the completion block
                    }
                    //如果有下载图片，且不是gif和需要转化图片
                    else if (downloadedImage && (!downloadedImage.images || (options & SDWebImageTransformAnimatedImage)) && [self.delegate respondsToSelector:@selector(imageManager:transformDownloadedImage:withURL:)]) {
                        //子线程转化图片
                        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^{
                            UIImage *transformedImage = [self.delegate imageManager:self transformDownloadedImage:downloadedImage withURL:url];
                            //如果转化图片存在并且结束了
                            if (transformedImage && finished) {
                                //是否图片被转换了
                                BOOL imageWasTransformed = ![transformedImage isEqual:downloadedImage];
                                //将图片存入cache中
                                [self.imageCache storeImage:transformedImage recalculateFromImage:imageWasTransformed imageData:(imageWasTransformed ? nil : data) forKey:key toDisk:cacheOnDisk];
                            }

                            dispatch_main_sync_safe(^{
                                if (strongOperation && !strongOperation.isCancelled) {
                                    completedBlock(transformedImage, nil, SDImageCacheTypeNone, finished, url);
                                }
                            });
                        });
                    }
                    else {
                        //如果下载图片并且完成了
                        if (downloadedImage && finished) {
                            //存储图片
                            [self.imageCache storeImage:downloadedImage recalculateFromImage:NO imageData:data forKey:key toDisk:cacheOnDisk];
                        }

                        dispatch_main_sync_safe(^{
                            if (strongOperation && !strongOperation.isCancelled) {
                                completedBlock(downloadedImage, nil, SDImageCacheTypeNone, finished, url);
                            }
                        });
                    }
                }
                
                //如果结束了,将operation从存储中删除
                if (finished) {
                    @synchronized (self.runningOperations) {
                        if (strongOperation) {
                            [self.runningOperations removeObject:strongOperation];
                        }
                    }
                }
            }];
            //设置的取消block
            operation.cancelBlock = ^{
                //下载取消
                [subOperation cancel];
                
                //并且从operation中删除
                @synchronized (self.runningOperations) {
                    __strong __typeof(weakOperation) strongOperation = weakOperation;
                    if (strongOperation) {
                        [self.runningOperations removeObject:strongOperation];
                    }
                }
            };
        }
        //如果有图片
        else if (image) {
            dispatch_main_sync_safe(^{
                __strong __typeof(weakOperation) strongOperation = weakOperation;
                if (strongOperation && !strongOperation.isCancelled) {
                    completedBlock(image, nil, cacheType, YES, url);
                }
            });
            @synchronized (self.runningOperations) {
                [self.runningOperations removeObject:operation];
            }
        }
        else {
            // Image not in cache and download disallowed by delegate
            //图片在cache中不存在，并且下载不允许
            dispatch_main_sync_safe(^{
                __strong __typeof(weakOperation) strongOperation = weakOperation;
                if (strongOperation && !weakOperation.isCancelled) {
                    completedBlock(nil, nil, SDImageCacheTypeNone, YES, url);
                }
            });
            @synchronized (self.runningOperations) {
                [self.runningOperations removeObject:operation];
            }
        }
    }];

    return operation;
}
```
这里建造了``SDWebImageCombinedOperation``的对象，去存储``SDImageCache``查询图片缓存的operation，然后再查询缓存的block中，嵌套了一个``SDWebImageDownloader``下载图片的subOperation。在``SDWebImageCombinedOperation``的cancelBlock中去设置了subOperation的取消操作。

##SDWebImagePrefetcher
SDWebImagePrefetcher方法主要是用于部分图片需要先行下载并存储的情况。
主要设计了两种回调方式
* 1.SDWebImagePrefetcherDelegate
用来处理每一个预下载完成的回调，以及所有下载完成的回调
* 2.block
用来处理整体进度的回调，返回的是下载完成的数量和总数量等

相关实现如下：
```
@protocol SDWebImagePrefetcherDelegate <NSObject>

@optional
/**
 当一张图片预加载的时候调用

 @param imagePrefetcher 当前图片的预加载类
 @param imageURL        图片url
 @param finishedCount   已经预加载的数量
 @param totalCount      总共预加载的图片数量
 */
- (void)imagePrefetcher:(SDWebImagePrefetcher *)imagePrefetcher didPrefetchURL:(NSURL *)imageURL finishedCount:(NSUInteger)finishedCount totalCount:(NSUInteger)totalCount;

/**
 当所有图片被预加载时候调用

 @param imagePrefetcher 当前图片的预加载类
 @param totalCount      总共预加载的图片数量
 @param skippedCount    跳过的数量
 */
- (void)imagePrefetcher:(SDWebImagePrefetcher *)imagePrefetcher didFinishWithTotalCount:(NSUInteger)totalCount skippedCount:(NSUInteger)skippedCount;

@end
/**
 预加载进度block

 @param noOfFinishedUrls 已经完成的数量，无论成功失败
 @param noOfTotalUrls    总数量
 */
typedef void(^SDWebImagePrefetcherProgressBlock)(NSUInteger noOfFinishedUrls, NSUInteger noOfTotalUrls);
/**
 预加载完成block

 @param noOfFinishedUrls 已经完成的数量，无论成功失败
 @param noOfSkippedUrls  跳过的数量
 */
typedef void(^SDWebImagePrefetcherCompletionBlock)(NSUInteger noOfFinishedUrls, NSUInteger noOfSkippedUrls);
```
SDWebImagePrefetcher的主要加载方法如下：

```
//开始加载
- (void)startPrefetchingAtIndex:(NSUInteger)index {
    if (index >= self.prefetchURLs.count) return;
    //已经请求数量＋1
    self.requestedCount++;
    [self.manager downloadImageWithURL:self.prefetchURLs[index] options:self.options progress:nil completed:^(UIImage *image, NSError *error, SDImageCacheType cacheType, BOOL finished, NSURL *imageURL) {
        if (!finished) return;
        //完成的请求数量＋1
        self.finishedCount++;

        if (image) {
            if (self.progressBlock) {
                self.progressBlock(self.finishedCount,[self.prefetchURLs count]);
            }
        }
        else {
            if (self.progressBlock) {
                self.progressBlock(self.finishedCount,[self.prefetchURLs count]);
            }
            // Add last failed
            //失败的请求数量—＋1
            self.skippedCount++;
        }
        //回调下载的过程中的回调
        if ([self.delegate respondsToSelector:@selector(imagePrefetcher:didPrefetchURL:finishedCount:totalCount:)]) {
            [self.delegate imagePrefetcher:self
                            didPrefetchURL:self.prefetchURLs[index]
                             finishedCount:self.finishedCount
                                totalCount:self.prefetchURLs.count
             ];
        }
        //总数>请求数，说明还没请求完
        if (self.prefetchURLs.count > self.requestedCount) {
            dispatch_async(self.prefetcherQueue, ^{
                [self startPrefetchingAtIndex:self.requestedCount];
            });
        //全部请求完成
        } else if (self.finishedCount == self.requestedCount) {
            //完成回调
            [self reportStatus];
            if (self.completionBlock) {
                self.completionBlock(self.finishedCount, self.skippedCount);
                self.completionBlock = nil;
            }
            self.progressBlock = nil;
        }
    }];
}

//完成状态回调
- (void)reportStatus {
    //获得所有加载数量，回调delegate
    NSUInteger total = [self.prefetchURLs count];
    if ([self.delegate respondsToSelector:@selector(imagePrefetcher:didFinishWithTotalCount:skippedCount:)]) {
        [self.delegate imagePrefetcher:self
               didFinishWithTotalCount:(total - self.skippedCount)
                          skippedCount:self.skippedCount
         ];
    }
}

- (void)prefetchURLs:(NSArray *)urls {
    [self prefetchURLs:urls progress:nil completed:nil];
}
/**
 列所有需要预加载的url
 同时1张图图片只会下载1次
 跳过下载失败的图片并列岛下一个列表中

 @param urls            需要预加载的url列表
 @param progressBlock   进度block
 @param completionBlock 完成block
 */
- (void)prefetchURLs:(NSArray *)urls progress:(SDWebImagePrefetcherProgressBlock)progressBlock completed:(SDWebImagePrefetcherCompletionBlock)completionBlock {
    //防止重复的预加载请求
    [self cancelPrefetching]; // Prevent duplicate prefetch request
    self.startedTime = CFAbsoluteTimeGetCurrent();
    self.prefetchURLs = urls;
    self.completionBlock = completionBlock;
    self.progressBlock = progressBlock;

    if (urls.count == 0) {
        if (completionBlock) {
            completionBlock(0,0);
        }
    } else {
        // Starts prefetching from the very first image on the list with the max allowed concurrency
        NSUInteger listCount = self.prefetchURLs.count;
        for (NSUInteger i = 0; i < self.maxConcurrentDownloads && self.requestedCount < listCount; i++) {
            [self startPrefetchingAtIndex:i];
        }
    }
}

- (void)cancelPrefetching {
    self.prefetchURLs = nil;
    self.skippedCount = 0;
    self.requestedCount = 0;
    self.finishedCount = 0;
    [self.manager cancelAll];
}
```

##总结
Utils可以算是集合了前两章总结的Cache与Downloader的总和。提供了对此两种基础方法的一层封装，提供给外部使用。

##参考资料
1.[SDWebImage源码浅析](http://joakimliu.github.io/2015/11/15/Resolve-The-SourceCode-Of-SDWebImage/)
2.[UIColor，CGColor，CIColor三者的区别和联系](http://www.cnblogs.com/smileEvday/archive/2012/06/05/UIColor_CIColor_CGColor.html)
