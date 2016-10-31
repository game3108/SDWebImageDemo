##前言
CSDN地址：http://blog.csdn.net/game3108/article/details/52575740
本文的中文注释代码demo更新在我的[github](https://github.com/game3108/SDWebImageDemo)上。

[SDWebImage](https://github.com/rs/SDWebImage)是一个十分有名的Objective-C第三方开源框架，作用是：
> Asynchronous image downloader with cache support as a UIImageView category

一个异步的图片下载与缓存的``UIImageView``Category。

本文也将对SDWebImage的源代码进行一次简单的解析，当作学习和记录。
SDWebImage源代码较长，本文会分为一个一个部分进行解析。本次解析的部分就是SDImageCache。

##SDImageCache使用

SDWebImage的使用十分简单，比较常用的一个接口:
```
[cell.imageView sd_setImageWithURL:[NSURL URLWithString:@"http://www.domain.com/path/to/image.jpg"]
                      placeholderImage:[UIImage imageNamed:@"placeholder.png"]];
```
只需要一个url和一个placeholderimage，就可以下载图片和设置占位图。


##整体结构
SDWebImage整体项目结构图：

![SDWebImage](http://upload-images.jianshu.io/upload_images/1829891-b97bfac9169cc6dd.png?imageMogr2/auto-orient/strip%7CimageView2/2/w/1240)

主要分为以下几块：
1.头文件、宏定义、``cancel``接口
2.Downloader：下载图片
3.Cache:图片缓存
4.Utils:
``SDWebImageManager``:管理类
``SDWebImageDecoder``:图片解压缩类
``SDWebImagePrefetcher``:图片预加载类
5.Categories:相关类使用接口
##Cache
``SDImageCache``是用来缓存图片到内存以及硬盘，它主要包含以下几类方法：
* 创建Cache空间和路径
* 存储图片
* 读取图片
* 删除图片
* 删除缓存
* 清理缓存
* 获取硬盘缓存大小
* 判断key是否存在在硬盘

###创建Cache空间和路径
``SDImageCache``的初始化方法就是初始化一些基本的值和注册相关的notification。
相关方法如下：
```
//单例
+ (SDImageCache *)sharedImageCache;
//初始化一个namespace空间
- (id)initWithNamespace:(NSString *)ns;
//初始化一个namespace空间和存储的路径
- (id)initWithNamespace:(NSString *)ns diskCacheDirectory:(NSString *)directory;
//存储路径
-(NSString *)makeDiskCachePath:(NSString*)fullNamespace;
//添加只读cache的path
- (void)addReadOnlyCachePath:(NSString *)path;
```
实现如下：
```
//标准单例创建方法
+ (SDImageCache *)sharedImageCache {
    static dispatch_once_t once;
    static id instance;
    dispatch_once(&once, ^{
        instance = [self new];
    });
    return instance;
}

//添加只读的cachePath
- (void)addReadOnlyCachePath:(NSString *)path {
    //只读路径，只在查找图片时候使用
    if (!self.customPaths) {
        self.customPaths = [NSMutableArray new];
    }

    if (![self.customPaths containsObject:path]) {
        [self.customPaths addObject:path];
    }
}

//初始化默认方法
- (id)init {
    return [self initWithNamespace:@"default"];
}

- (id)initWithNamespace:(NSString *)ns {
    //获取路径，再初始化
    NSString *path = [self makeDiskCachePath:ns];
    return [self initWithNamespace:ns diskCacheDirectory:path];
}

- (id)initWithNamespace:(NSString *)ns diskCacheDirectory:(NSString *)directory {
    if ((self = [super init])) {
        NSString *fullNamespace = [@"com.hackemist.SDWebImageCache." stringByAppendingString:ns];

        // initialise PNG signature data
        //初始化PNG图片的标签数据
        kPNGSignatureData = [NSData dataWithBytes:kPNGSignatureBytes length:8];

        // Create IO serial queue
        //创建 serial queue
        _ioQueue = dispatch_queue_create("com.hackemist.SDWebImageCache", DISPATCH_QUEUE_SERIAL);

        // Init default values
        //默认存储时间一周
        _maxCacheAge = kDefaultCacheMaxCacheAge;

        // Init the memory cache
        //初始化内存cache对象
        _memCache = [[AutoPurgeCache alloc] init];
        _memCache.name = fullNamespace;

        // Init the disk cache
        //初始化硬盘缓存路径
        if (directory != nil) {
            _diskCachePath = [directory stringByAppendingPathComponent:fullNamespace];
        } else {
            //默认路径
            NSString *path = [self makeDiskCachePath:ns];
            _diskCachePath = path;
        }

        // Set decompression to YES
        //需要压缩图片
        _shouldDecompressImages = YES;

        // memory cache enabled
        //需要cache在内存
        _shouldCacheImagesInMemory = YES;

        // Disable iCloud
        //禁止icloud
        _shouldDisableiCloud = YES;

        dispatch_sync(_ioQueue, ^{
            _fileManager = [NSFileManager new];
        });

#if TARGET_OS_IOS
        // Subscribe to app events
        //app相关warning，包括内存warning，app关闭notification，到后台notification
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(clearMemory)
                                                     name:UIApplicationDidReceiveMemoryWarningNotification
                                                   object:nil];

        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(cleanDisk)
                                                     name:UIApplicationWillTerminateNotification
                                                   object:nil];

        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(backgroundCleanDisk)
                                                     name:UIApplicationDidEnterBackgroundNotification
                                                   object:nil];
#endif
    }

    return self;
}

```
其中需要说一下的是``kPNGSignatureData``，它是用来判断是否是PNG图片的前缀标签数据，它的初始化数据``kPNGSignatureBytes``的来源如下：
```
// PNG图片的标签值
static unsigned char kPNGSignatureBytes[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
```
###存储图片
存储图片主要分为2块：
* 存储到内存
直接计算图片大小后，用``NSCache *memCache``存储
* 存储到硬盘
先将UIImage转为NSData，然后通过``NSFileManager *_fileManager``创建存储路径文件夹和图片存储文件

相关方法如下：
```
//存储image到key
- (void)storeImage:(UIImage *)image forKey:(NSString *)key;

//存储image到key，是否硬盘缓存
- (void)storeImage:(UIImage *)image forKey:(NSString *)key toDisk:(BOOL)toDisk;

//存储image，是否重新计算图片，服务器的数据来源，key，是否硬盘缓存
- (void)storeImage:(UIImage *)image recalculateFromImage:(BOOL)recalculate imageData:(NSData *)imageData forKey:(NSString *)key toDisk:(BOOL)toDisk;

//硬盘缓存，key
- (void)storeImageDataToDisk:(NSData *)imageData forKey:(NSString *)key;

```

```
//存储方法
- (void)storeImage:(UIImage *)image forKey:(NSString *)key {
    [self storeImage:image recalculateFromImage:YES imageData:nil forKey:key toDisk:YES];
}

//存储方法
- (void)storeImage:(UIImage *)image forKey:(NSString *)key toDisk:(BOOL)toDisk {
    [self storeImage:image recalculateFromImage:YES imageData:nil forKey:key toDisk:toDisk];
}

//存储到硬盘
- (void)storeImageDataToDisk:(NSData *)imageData forKey:(NSString *)key {
    
    //没有图片返回
    if (!imageData) {
        return;
    }
    
    //判断是否存在硬盘存储的文件夹
    if (![_fileManager fileExistsAtPath:_diskCachePath]) {
        //没有泽创建
        [_fileManager createDirectoryAtPath:_diskCachePath withIntermediateDirectories:YES attributes:nil error:NULL];
    }
    
    // get cache Path for image key
    //获取key的完整存储路径
    NSString *cachePathForKey = [self defaultCachePathForKey:key];
    // transform to NSUrl
    NSURL *fileURL = [NSURL fileURLWithPath:cachePathForKey];
    
    //存储图片
    [_fileManager createFileAtPath:cachePathForKey contents:imageData attributes:nil];
    
    // disable iCloud backup
    //金庸icloud
    if (self.shouldDisableiCloud) {
        [fileURL setResourceValue:[NSNumber numberWithBool:YES] forKey:NSURLIsExcludedFromBackupKey error:nil];
    }
}

//存储图片基础方法
- (void)storeImage:(UIImage *)image recalculateFromImage:(BOOL)recalculate imageData:(NSData *)imageData forKey:(NSString *)key toDisk:(BOOL)toDisk {
    if (!image || !key) {
        return;
    }
    // if memory cache is enabled
    //是否缓存在内存中国年
    if (self.shouldCacheImagesInMemory) {
        //获取图片大小
        NSUInteger cost = SDCacheCostForImage(image);
        //设置缓存
        [self.memCache setObject:image forKey:key cost:cost];
    }

    //是否硬盘存储
    if (toDisk) {
        dispatch_async(self.ioQueue, ^{
            NSData *data = imageData;

            if (image && (recalculate || !data)) {
#if TARGET_OS_IPHONE
                // We need to determine if the image is a PNG or a JPEG
                // PNGs are easier to detect because they have a unique signature (http://www.w3.org/TR/PNG-Structure.html)
                // The first eight bytes of a PNG file always contain the following (decimal) values:
                // 137 80 78 71 13 10 26 10

                // If the imageData is nil (i.e. if trying to save a UIImage directly or the image was transformed on download)
                // and the image has an alpha channel, we will consider it PNG to avoid losing the transparency
                int alphaInfo = CGImageGetAlphaInfo(image.CGImage);
                //判断是否有alpha
                BOOL hasAlpha = !(alphaInfo == kCGImageAlphaNone ||
                                  alphaInfo == kCGImageAlphaNoneSkipFirst ||
                                  alphaInfo == kCGImageAlphaNoneSkipLast);
                //有alpha肯定是png
                BOOL imageIsPng = hasAlpha;

                // But if we have an image data, we will look at the preffix
                //查看图片前几个字节，是否是png
                if ([imageData length] >= [kPNGSignatureData length]) {
                    imageIsPng = ImageDataHasPNGPreffix(imageData);
                }

                //png图
                if (imageIsPng) {
                    data = UIImagePNGRepresentation(image);
                }
                //jpeg图
                else {
                    data = UIImageJPEGRepresentation(image, (CGFloat)1.0);
                }
#else
                data = [NSBitmapImageRep representationOfImageRepsInArray:image.representations usingType: NSJPEGFileType properties:nil];
#endif
            }
            //存储到硬盘
            [self storeImageDataToDisk:data forKey:key];
        });
    }
}
```
其中对于获取key的完整路径处理``NSString *cachePathForKey = [self defaultCachePathForKey:key];``，还做了md5的加密，具体实现如下：
```
//path组合key
- (NSString *)cachePathForKey:(NSString *)key inPath:(NSString *)path {
    NSString *filename = [self cachedFileNameForKey:key];
    return [path stringByAppendingPathComponent:filename];
}

//默认路径的keypath
- (NSString *)defaultCachePathForKey:(NSString *)key {
    return [self cachePathForKey:key inPath:self.diskCachePath];
}

#pragma mark SDImageCache (private)

//MD5加密key
- (NSString *)cachedFileNameForKey:(NSString *)key {
    const char *str = [key UTF8String];
    if (str == NULL) {
        str = "";
    }
    unsigned char r[CC_MD5_DIGEST_LENGTH];
    CC_MD5(str, (CC_LONG)strlen(str), r);
    NSString *filename = [NSString stringWithFormat:@"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%@",
                          r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10],
                          r[11], r[12], r[13], r[14], r[15], [[key pathExtension] isEqualToString:@""] ? @"" : [NSString stringWithFormat:@".%@", [key pathExtension]]];

    return filename;
}

```

###读取图片
读取图片主要分为两步：
* 内存cache读取
直接读取内存cache``self.memCache``
* 硬盘图片读取
先从内存读取cache，如果不存在，则从硬盘读取，并且有必要的情况下存储到内存cache中

相关方法如下：
```
//cache存储位置enum
typedef NS_ENUM(NSInteger, SDImageCacheType) {
    /**
     * The image wasn't available the SDWebImage caches, but was downloaded from the web.
     */
    SDImageCacheTypeNone,
    /**
     * The image was obtained from the disk cache.
     */
    SDImageCacheTypeDisk,
    /**
     * The image was obtained from the memory cache.
     */
    SDImageCacheTypeMemory
};

//3个cache block
typedef void(^SDWebImageQueryCompletedBlock)(UIImage *image, SDImageCacheType cacheType);

//异步请求缓存图片，通过block
- (NSOperation *)queryDiskCacheForKey:(NSString *)key done:(SDWebImageQueryCompletedBlock)doneBlock;

//直接读取cache的图片
- (UIImage *)imageFromMemoryCacheForKey:(NSString *)key;

//通过key，直接读取硬盘
- (UIImage *)imageFromDiskCacheForKey:(NSString *)key;
```

```
//内存直接读取image
- (UIImage *)imageFromMemoryCacheForKey:(NSString *)key {
    return [self.memCache objectForKey:key];
}

//硬盘直接读取image
- (UIImage *)imageFromDiskCacheForKey:(NSString *)key {

    // First check the in-memory cache...
    //先检查内存中是否含有
    UIImage *image = [self imageFromMemoryCacheForKey:key];
    if (image) {
        return image;
    }

    // Second check the disk cache...
    //获取硬盘存储的image
    UIImage *diskImage = [self diskImageForKey:key];
    //如果获取到了，并且需要存储到内存cache
    if (diskImage && self.shouldCacheImagesInMemory) {
        //存储到cache
        NSUInteger cost = SDCacheCostForImage(diskImage);
        [self.memCache setObject:diskImage forKey:key cost:cost];
    }

    return diskImage;
}
//异步获取图片
- (NSOperation *)queryDiskCacheForKey:(NSString *)key done:(SDWebImageQueryCompletedBlock)doneBlock {
    if (!doneBlock) {
        return nil;
    }
    
    //没有key，则返回不存在
    if (!key) {
        doneBlock(nil, SDImageCacheTypeNone);
        return nil;
    }

    // First check the in-memory cache...
    //先获取memory中的图片
    UIImage *image = [self imageFromMemoryCacheForKey:key];
    if (image) {
        doneBlock(image, SDImageCacheTypeMemory);
        return nil;
    }

    NSOperation *operation = [NSOperation new];
    //硬盘查找速度较慢，在子线程中查找
    dispatch_async(self.ioQueue, ^{
        if (operation.isCancelled) {
            return;
        }
        //创建autoreleasepool
        @autoreleasepool {
            //获取硬盘图片
            UIImage *diskImage = [self diskImageForKey:key];
            //如果获取到了，并且需要存储到内存cache
            if (diskImage && self.shouldCacheImagesInMemory) {
                //获得大小并存储图片
                NSUInteger cost = SDCacheCostForImage(diskImage);
                [self.memCache setObject:diskImage forKey:key cost:cost];
            }
            //回到主线程返回结果
            dispatch_async(dispatch_get_main_queue(), ^{
                doneBlock(diskImage, SDImageCacheTypeDisk);
            });
        }
    });

    return operation;
}
```
其中，对于从硬盘存储获取图片``UIImage *diskImage = [self diskImageForKey:key];``的实现如下：
```
//硬盘返回图片
- (UIImage *)diskImageForKey:(NSString *)key {
    //搜索所有的path，然后把这个key的图片拿出来
    NSData *data = [self diskImageDataBySearchingAllPathsForKey:key];
    //如果有data
    if (data) {
        //data转为image，实现在UIImage+MultiFormat中
        UIImage *image = [UIImage sd_imageWithData:data];
        //图片适配屏幕
        image = [self scaledImageForKey:key image:image];
        //压缩图片，实现在SDWebImageDecoder
        if (self.shouldDecompressImages) {
            image = [UIImage decodedImageWithImage:image];
        }
        return image;
    }
    else {
        return nil;
    }
}

//适配屏幕
- (UIImage *)scaledImageForKey:(NSString *)key image:(UIImage *)image {
    //方法在SDWebImageCompat中
    return SDScaledImageForKey(key, image);
}

//从所有path中读取图片
- (NSData *)diskImageDataBySearchingAllPathsForKey:(NSString *)key {
    //默认读取路径
    NSString *defaultPath = [self defaultCachePathForKey:key];
    NSData *data = [NSData dataWithContentsOfFile:defaultPath];
    //读取到则返回
    if (data) {
        return data;
    }

    // fallback because of https://github.com/rs/SDWebImage/pull/976 that added the extension to the disk file name
    // checking the key with and without the extension
    //去掉path extension，这块应该是为了兼容某一个版本添加的path extension，所以本质上是个兼容旧版本的逻辑判断
    data = [NSData dataWithContentsOfFile:[defaultPath stringByDeletingPathExtension]];
    if (data) {
        return data;
    }

    //本地只读路径读取
    NSArray *customPaths = [self.customPaths copy];
    for (NSString *path in customPaths) {
        //获取只读路径中的filepath
        NSString *filePath = [self cachePathForKey:key inPath:path];
        NSData *imageData = [NSData dataWithContentsOfFile:filePath];
        if (imageData) {
            return imageData;
        }

        // fallback because of https://github.com/rs/SDWebImage/pull/976 that added the extension to the disk file name
        // checking the key with and without the extension
        //同上
        imageData = [NSData dataWithContentsOfFile:[filePath stringByDeletingPathExtension]];
        if (imageData) {
            return imageData;
        }
    }

    return nil;
}
```
这边牵扯到好几个其他类的方法，这边就不先标注它们的源代码了，等到讲到的时候再讲一下。

###删除图片
删除图片方法分为两步：
* 先从cache删除缓存图片
* 然后硬盘删除对应文件

相关方法如下：
```
//通过key，删除图片
- (void)removeImageForKey:(NSString *)key;

//异步删除图片，调用完成block
- (void)removeImageForKey:(NSString *)key withCompletion:(SDWebImageNoParamsBlock)completion;

//删除图片，同时从硬盘删除
- (void)removeImageForKey:(NSString *)key fromDisk:(BOOL)fromDisk;

//异步删除，同时删除硬盘
- (void)removeImageForKey:(NSString *)key fromDisk:(BOOL)fromDisk withCompletion:(SDWebImageNoParamsBlock)completion;
```
```
//删除图片
- (void)removeImageForKey:(NSString *)key {
    [self removeImageForKey:key withCompletion:nil];
}

//删除图片
- (void)removeImageForKey:(NSString *)key withCompletion:(SDWebImageNoParamsBlock)completion {
    [self removeImageForKey:key fromDisk:YES withCompletion:completion];
}

//删除图片
- (void)removeImageForKey:(NSString *)key fromDisk:(BOOL)fromDisk {
    [self removeImageForKey:key fromDisk:fromDisk withCompletion:nil];
}

//删除图片基础方法
- (void)removeImageForKey:(NSString *)key fromDisk:(BOOL)fromDisk withCompletion:(SDWebImageNoParamsBlock)completion {
    
    //不存在key返回
    if (key == nil) {
        return;
    }

    //需要用memory，先从cache删除图片
    if (self.shouldCacheImagesInMemory) {
        [self.memCache removeObjectForKey:key];
    }
    
    //硬盘删除
    if (fromDisk) {
        dispatch_async(self.ioQueue, ^{
            //直接删除路径下key对应的文件名字
            [_fileManager removeItemAtPath:[self defaultCachePathForKey:key] error:nil];
            
            if (completion) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    completion();
                });
            }
        });
    } else if (completion){
        completion();
    }
    
}
```
###删除缓存
删除缓存也分为两块：
* 直接清空缓存``memCache``
* 删除硬盘存储目录，再重新创建目录

相关方法
```
//删除缓存图片
- (void)clearMemory;

//删除硬盘存储图片，调用完成block
- (void)clearDiskOnCompletion:(SDWebImageNoParamsBlock)completion;

//删除硬盘存储图片
- (void)clearDisk;
```

```
//轻松memcache
- (void)clearMemory {
    [self.memCache removeAllObjects];
}

//删除硬盘存储
- (void)clearDisk {
    [self clearDiskOnCompletion:nil];
}

//删除硬盘存储基础方法
- (void)clearDiskOnCompletion:(SDWebImageNoParamsBlock)completion
{
    dispatch_async(self.ioQueue, ^{
        //直接删除存文件的路径
        [_fileManager removeItemAtPath:self.diskCachePath error:nil];
        //重新创建文件路径
        [_fileManager createDirectoryAtPath:self.diskCachePath
                withIntermediateDirectories:YES
                                 attributes:nil
                                      error:NULL];

        if (completion) {
            dispatch_async(dispatch_get_main_queue(), ^{
                completion();
            });
        }
    });
}
```

###清理缓存
清理缓存的目标是将过期的图片和确保硬盘存储大小小于最大存储许可。
清理缓存的方法也是分为两步：
*  清除所有过期图片
* 在硬盘存储大小大于最大存储许可大小时，将旧图片进行删除，删除到一半最大许可以下

相关方法:
```
//清理硬盘缓存图片
- (void)cleanDiskWithCompletionBlock:(SDWebImageNoParamsBlock)completionBlock;

//清理硬盘缓存图片
- (void)cleanDisk;
```

```
//清理硬盘
- (void)cleanDisk {
    [self cleanDiskWithCompletionBlock:nil];
}

//清理硬盘基础方法
//清理硬盘的目的是为了缓解硬盘压力，以及过期图片，超大小图片等
- (void)cleanDiskWithCompletionBlock:(SDWebImageNoParamsBlock)completionBlock {
    dispatch_async(self.ioQueue, ^{
        //硬盘存储路径获取
        NSURL *diskCacheURL = [NSURL fileURLWithPath:self.diskCachePath isDirectory:YES];
        //是否是目录，获取修改日期，获取size大小
        NSArray *resourceKeys = @[NSURLIsDirectoryKey, NSURLContentModificationDateKey, NSURLTotalFileAllocatedSizeKey];

        // This enumerator prefetches useful properties for our cache files.
        //迭代器遍历
        NSDirectoryEnumerator *fileEnumerator = [_fileManager enumeratorAtURL:diskCacheURL
                                                   includingPropertiesForKeys:resourceKeys
                                                                      options:NSDirectoryEnumerationSkipsHiddenFiles
                                                                 errorHandler:NULL];
        //获取应该过期的日期
        NSDate *expirationDate = [NSDate dateWithTimeIntervalSinceNow:-self.maxCacheAge];
        //缓存路径与3个key值
        NSMutableDictionary *cacheFiles = [NSMutableDictionary dictionary];
        //获取总的目录下文件大小
        NSUInteger currentCacheSize = 0;

        // Enumerate all of the files in the cache directory.  This loop has two purposes:
        //
        //  1. Removing files that are older than the expiration date.
        //  2. Storing file attributes for the size-based cleanup pass.
        
        //需要删除的路径存储
        NSMutableArray *urlsToDelete = [[NSMutableArray alloc] init];
        //遍历路径下的所有文件于文件夹
        for (NSURL *fileURL in fileEnumerator) {
            //获取该文件路径的3种值
            NSDictionary *resourceValues = [fileURL resourceValuesForKeys:resourceKeys error:NULL];

            // Skip directories.
            //跳过目录
            if ([resourceValues[NSURLIsDirectoryKey] boolValue]) {
                continue;
            }

            // Remove files that are older than the expiration date;
            //获取修改日期
            NSDate *modificationDate = resourceValues[NSURLContentModificationDateKey];
            //在有效期日期之前的文件需要删除，添加到
            if ([[modificationDate laterDate:expirationDate] isEqualToDate:expirationDate]) {
                [urlsToDelete addObject:fileURL];
                continue;
            }

            // Store a reference to this file and account for its total size.
            NSNumber *totalAllocatedSize = resourceValues[NSURLTotalFileAllocatedSizeKey];
            //获取文件大小
            currentCacheSize += [totalAllocatedSize unsignedIntegerValue];
            //存储值
            [cacheFiles setObject:resourceValues forKey:fileURL];
        }
        
        //删除所有的过期图片
        for (NSURL *fileURL in urlsToDelete) {
            [_fileManager removeItemAtURL:fileURL error:nil];
        }

        // If our remaining disk cache exceeds a configured maximum size, perform a second
        // size-based cleanup pass.  We delete the oldest files first.
        
        //如果当前硬盘存储大于最大缓存，则删除一半的硬盘存储，先删除老的图片
        if (self.maxCacheSize > 0 && currentCacheSize > self.maxCacheSize) {
            // Target half of our maximum cache size for this cleanup pass.
            
            //目标空间
            const NSUInteger desiredCacheSize = self.maxCacheSize / 2;

            // Sort the remaining cache files by their last modification time (oldest first).
            
            //排序所有filepath，时间排序
            NSArray *sortedFiles = [cacheFiles keysSortedByValueWithOptions:NSSortConcurrent
                                                            usingComparator:^NSComparisonResult(id obj1, id obj2) {
                                                                return [obj1[NSURLContentModificationDateKey] compare:obj2[NSURLContentModificationDateKey]];
                                                            }];

            // Delete files until we fall below our desired cache size.
            //删除文件，直到小于目标空间停止
            for (NSURL *fileURL in sortedFiles) {
                if ([_fileManager removeItemAtURL:fileURL error:nil]) {
                    NSDictionary *resourceValues = cacheFiles[fileURL];
                    NSNumber *totalAllocatedSize = resourceValues[NSURLTotalFileAllocatedSizeKey];
                    currentCacheSize -= [totalAllocatedSize unsignedIntegerValue];

                    if (currentCacheSize < desiredCacheSize) {
                        break;
                    }
                }
            }
        }
        //回调block
        if (completionBlock) {
            dispatch_async(dispatch_get_main_queue(), ^{
                completionBlock();
            });
        }
    });
}
```
这边需要提一下，当app进入后台，发送notification，调用的方法，就是清理硬盘内存，方法如下：
```
//后台清理硬盘
- (void)backgroundCleanDisk {
    Class UIApplicationClass = NSClassFromString(@"UIApplication");
    if(!UIApplicationClass || ![UIApplicationClass respondsToSelector:@selector(sharedApplication)]) {
        return;
    }
    UIApplication *application = [UIApplication performSelector:@selector(sharedApplication)];
    //设置后台存活，在ios7以上最多存在3分钟，7以下是10分钟
    __block UIBackgroundTaskIdentifier bgTask = [application beginBackgroundTaskWithExpirationHandler:^{
        // Clean up any unfinished task business by marking where you
        // stopped or ending the task outright.
        [application endBackgroundTask:bgTask];
        bgTask = UIBackgroundTaskInvalid;
    }];
    
    //在存活期间清理硬盘
    // Start the long-running task and return immediately.
    [self cleanDiskWithCompletionBlock:^{
        [application endBackgroundTask:bgTask];
        bgTask = UIBackgroundTaskInvalid;
    }];
}
```

###获取硬盘缓存大小
获取硬盘缓存大小直接读取硬盘路径下的文件数量和每个文件大小，进行累加。
相关方法如下：
```
typedef void(^SDWebImageCalculateSizeBlock)(NSUInteger fileCount, NSUInteger totalSize);
//获得硬盘存储空间大小
- (NSUInteger)getSize;

//获得硬盘空间图片数量
- (NSUInteger)getDiskCount;

//异步计算硬盘空间大小
- (void)calculateSizeWithCompletionBlock:(SDWebImageCalculateSizeBlock)completionBlock;
```

```
//获取硬盘存储的大小
- (NSUInteger)getSize {
    __block NSUInteger size = 0;
    dispatch_sync(self.ioQueue, ^{
        //硬盘路径迭代器
        NSDirectoryEnumerator *fileEnumerator = [_fileManager enumeratorAtPath:self.diskCachePath];
        for (NSString *fileName in fileEnumerator) {
            //拼接路径
            NSString *filePath = [self.diskCachePath stringByAppendingPathComponent:fileName];
            //获取路径下的文件与文件夹属性
            NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:filePath error:nil];
            //算大小
            size += [attrs fileSize];
        }
    });
    return size;
}

//获取硬盘存储的数量
- (NSUInteger)getDiskCount {
    __block NSUInteger count = 0;
    dispatch_sync(self.ioQueue, ^{
        //硬盘路径迭代器
        NSDirectoryEnumerator *fileEnumerator = [_fileManager enumeratorAtPath:self.diskCachePath];
        count = [[fileEnumerator allObjects] count];
    });
    return count;
}

//异步获取硬盘缓存大小
- (void)calculateSizeWithCompletionBlock:(SDWebImageCalculateSizeBlock)completionBlock {
    NSURL *diskCacheURL = [NSURL fileURLWithPath:self.diskCachePath isDirectory:YES];

    dispatch_async(self.ioQueue, ^{
        NSUInteger fileCount = 0;
        NSUInteger totalSize = 0;
        
        //迭代所有路径下文件
        NSDirectoryEnumerator *fileEnumerator = [_fileManager enumeratorAtURL:diskCacheURL
                                                   includingPropertiesForKeys:@[NSFileSize]
                                                                      options:NSDirectoryEnumerationSkipsHiddenFiles
                                                                 errorHandler:NULL];
        //遍历文件和文件夹
        for (NSURL *fileURL in fileEnumerator) {
            NSNumber *fileSize;
            //获取size
            [fileURL getResourceValue:&fileSize forKey:NSURLFileSizeKey error:NULL];
            totalSize += [fileSize unsignedIntegerValue];
            fileCount += 1;
        }

        if (completionBlock) {
            dispatch_async(dispatch_get_main_queue(), ^{
                completionBlock(fileCount, totalSize);
            });
        }
    });
}
```

###判断key是否存在在硬盘
判断key是否存在在硬盘中，直接从硬盘中判断是否存在此key的文件路径
相关方法如下：
```
typedef void(^SDWebImageCheckCacheCompletionBlock)(BOOL isInCache);
//异步判断是否存在key的图片
- (void)diskImageExistsWithKey:(NSString *)key completion:(SDWebImageCheckCacheCompletionBlock)completionBlock;

//同步判断是否存在key的图片
- (BOOL)diskImageExistsWithKey:(NSString *)key;
```
```
//直接读取
- (BOOL)diskImageExistsWithKey:(NSString *)key {
    BOOL exists = NO;
    
    // this is an exception to access the filemanager on another queue than ioQueue, but we are using the shared instance
    // from apple docs on NSFileManager: The methods of the shared NSFileManager object can be called from multiple threads safely.
    exists = [[NSFileManager defaultManager] fileExistsAtPath:[self defaultCachePathForKey:key]];

    // fallback because of https://github.com/rs/SDWebImage/pull/976 that added the extension to the disk file name
    // checking the key with and without the extension
    if (!exists) {
        exists = [[NSFileManager defaultManager] fileExistsAtPath:[[self defaultCachePathForKey:key] stringByDeletingPathExtension]];
    }
    
    return exists;
}

//判断是否有key的文件
- (void)diskImageExistsWithKey:(NSString *)key completion:(SDWebImageCheckCacheCompletionBlock)completionBlock {
    dispatch_async(_ioQueue, ^{
        //直接去读默认文件
        BOOL exists = [_fileManager fileExistsAtPath:[self defaultCachePathForKey:key]];

        // fallback because of https://github.com/rs/SDWebImage/pull/976 that added the extension to the disk file name
        // checking the key with and without the extension
        
        //不存在去读区去掉path extension的方式
        if (!exists) {
            exists = [_fileManager fileExistsAtPath:[[self defaultCachePathForKey:key] stringByDeletingPathExtension]];
        }
        
        //回调block
        if (completionBlock) {
            dispatch_async(dispatch_get_main_queue(), ^{
                completionBlock(exists);
            });
        }
    });
}
```

##总结
SDWebImage本身牵扯到很多内容，这篇文章先整体讲了一下SDImageCache的设计与实现，它有如下优点：
* 1.memory的cache和硬盘存储结合存储
* 2.后台清理硬盘存储空间
* 3.NSURL的文件读取用途和相关文件夹迭代器和获取
* 4.精妙的kPNGSignatureData判断是否是PNG图片
* 5.分别提供同步和异步的方法

当然也有一个path extension文件名问题。
**这也告诉我们一个道理，很多时候文件名一定要想好怎么去存储，一旦进行修改了，后续兼容会很麻烦。**

##其他
本来是想SDWebImage解析完后与[YYWebImage](https://github.com/ibireme/YYWebImage)对比一下的，但既然都已经分文章去单独写了SDImageCache了，下篇文章就先解析[YYCache](https://github.com/ibireme/YYCache)。

***
看了看YYCache的源代码，确实比较长，还是先把SDWebImage分析完。

##参考资料
1.[Avoiding Image Decompression Sickness](https://www.cocoanetics.com/2011/10/avoiding-image-decompression-sickness/)
2.[SDWebImage源码浅析](http://joakimliu.github.io/2015/11/15/Resolve-The-SourceCode-Of-SDWebImage/)
