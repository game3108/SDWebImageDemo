##前言
CSDN地址：http://blog.csdn.net/game3108/article/details/52700626
本文的中文注释代码demo更新在我的[github](https://github.com/game3108/SDWebImageDemo)上。

[上篇文章](http://www.jianshu.com/p/f007dca390f0)讲解的了SDWebImage的Utils部分，这篇讲讲一下最后的Categories部分。

##Categories
Categories包含以下几个类文件：
* MKAnnotationView+WebCache
* NSData+ImageContentType
* UIButton+WebCache
* UIImage+GIF
* UIImage+MultiFormat
* UIImage+WebP
* UIImageView+HighlightedWebCache
* UIImageView+WebCache
* UIView+WebCacheOperation

这边只介绍一下``UIView+WebCacheOperation``与``UIImageView+WebCache``，其他文件类似，这里也不多展开了，在github的注释里会尽量都看一遍。

##UIView+WebCacheOperation
UIView+WebCacheOperation主要通过associateObject的方式，去缓存和取消、删除运行的操作。
相关方法定义如下：
```
/**
 设置图片的load操作(存入一个uiview的字典)

 @param operation 操作
 @param key       存入的key
 */
- (void)sd_setImageLoadOperation:(id)operation forKey:(NSString *)key;

/**
 取消当前uiview的key的操作

 @param key 存入的key
 */
- (void)sd_cancelImageLoadOperationWithKey:(NSString *)key;

/**
 移除当前uiview的key的操作，并且不取消他们

 @param key 存入的key
 */
- (void)sd_removeImageLoadOperationWithKey:(NSString *)key;
```
相关实现：
```
static char loadOperationKey;

- (NSMutableDictionary *)operationDictionary {
    //通过associateobject去存入和获取dictionaryu
    NSMutableDictionary *operations = objc_getAssociatedObject(self, &loadOperationKey);
    if (operations) {
        return operations;
    }
    operations = [NSMutableDictionary dictionary];
    objc_setAssociatedObject(self, &loadOperationKey, operations, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return operations;
}

- (void)sd_setImageLoadOperation:(id)operation forKey:(NSString *)key {
    //先取消操作
    [self sd_cancelImageLoadOperationWithKey:key];
    //然后获取dictionary并存入
    NSMutableDictionary *operationDictionary = [self operationDictionary];
    [operationDictionary setObject:operation forKey:key];
}

- (void)sd_cancelImageLoadOperationWithKey:(NSString *)key {
    // Cancel in progress downloader from queue
    //获取dictionary，并且取消在queue中的操作
    NSMutableDictionary *operationDictionary = [self operationDictionary];
    id operations = [operationDictionary objectForKey:key];
    if (operations) {
        //数组的话获取内部所有的取消
        if ([operations isKindOfClass:[NSArray class]]) {
            for (id <SDWebImageOperation> operation in operations) {
                if (operation) {
                    [operation cancel];
                }
            }
            //如果是实现SDWebImageOperation契约，直接取消
        } else if ([operations conformsToProtocol:@protocol(SDWebImageOperation)]){
            [(id<SDWebImageOperation>) operations cancel];
        }
        [operationDictionary removeObjectForKey:key];
    }
}

- (void)sd_removeImageLoadOperationWithKey:(NSString *)key {
    //获取dictionary并直接删除key
    NSMutableDictionary *operationDictionary = [self operationDictionary];
    [operationDictionary removeObjectForKey:key];
}
```

##UIImageView+WebCache
UIImageView+WebCache就是UIImage用到的sdwebimage的category，用户最外层的调用方式。
这边贴其中最基础的操作方法的定义和实现：

```
/**
 通过url，设置imageview的image，placeholder和设置
 
 下载方式异步并且缓存
 
 @param url            图片url
 @param placeholder    初始化图片
 @param options        下载图片方式
 @param progressBlock  进度block回调
 @param completedBlock 完成block回调
 */
- (void)sd_setImageWithURL:(NSURL *)url placeholderImage:(UIImage *)placeholder options:(SDWebImageOptions)options progress:(SDWebImageDownloaderProgressBlock)progressBlock completed:(SDWebImageCompletionBlock)completedBlock {
    //取消当前的图片下载
    [self sd_cancelCurrentImageLoad];
    //保存url
    objc_setAssociatedObject(self, &imageURLKey, url, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    //如果没有SDWebImageDelayPlaceholder则先设置placeholder图
    if (!(options & SDWebImageDelayPlaceholder)) {
        dispatch_main_async_safe(^{
            self.image = placeholder;
        });
    }
    
    //有url进行下载操作
    if (url) {

        // check if activityView is enabled or not
        //是否需要显示loading标签图
        if ([self showActivityIndicatorView]) {
            [self addActivityIndicator];
        }

        //下载图片
        __weak __typeof(self)wself = self;
        id <SDWebImageOperation> operation = [SDWebImageManager.sharedManager downloadImageWithURL:url options:options progress:progressBlock completed:^(UIImage *image, NSError *error, SDImageCacheType cacheType, BOOL finished, NSURL *imageURL) {
            
            //去掉可能存在的loadting图
            [wself removeActivityIndicator];
            if (!wself) return;
            dispatch_main_sync_safe(^{
                if (!wself) return;
                
                //不自动设置图片
                if (image && (options & SDWebImageAvoidAutoSetImage) && completedBlock)
                {
                    completedBlock(image, error, cacheType, url);
                    return;
                }
                //设置图片
                else if (image) {
                    wself.image = image;
                    [wself setNeedsLayout];
                } else {
                    //没有图片设置默认图
                    if ((options & SDWebImageDelayPlaceholder)) {
                        wself.image = placeholder;
                        [wself setNeedsLayout];
                    }
                }
                if (completedBlock && finished) {
                    completedBlock(image, error, cacheType, url);
                }
            });
        }];
        //缓存operation的key为load
        [self sd_setImageLoadOperation:operation forKey:@"UIImageViewImageLoad"];
    } else {
        //没有url直接去掉loading图
        dispatch_main_async_safe(^{
            [self removeActivityIndicator];
            //返回错误给外面的block
            if (completedBlock) {
                NSError *error = [NSError errorWithDomain:SDWebImageErrorDomain code:-1 userInfo:@{NSLocalizedDescriptionKey : @"Trying to load a nil url"}];
                completedBlock(nil, error, SDImageCacheTypeNone, url);
            }
        });
    }
}
```

##总结
到这里，SDWebImage就算全部解析完了。
我个人感觉，里面比较精髓的是处理图片的一些细节，比如gif图片，还有decode图片，用空间换时间等一些做法，这些细节的实现是整个图片处理的基础。
在框架上SDWebImage也是进行了多层的封装，将最基础的网络操作封装成operation，再此基础上再往上封装一层缓存层，并用一个manager去进行管理。而用户的使用调用都在category中实现相应的方法。

据说现在正在做4.0版本，会有一些大的变化改动，可以期待一下。

##参考资料
1.[SDWebImage源码浅析](http://joakimliu.github.io/2015/11/15/Resolve-The-SourceCode-Of-SDWebImage/)
