##前言
CSDN地址：http://blog.csdn.net/game3108/article/details/52598835
本文的中文注释代码demo更新在我的[github](https://github.com/game3108/SDWebImageDemo)上。

[上篇文章](http://www.jianshu.com/p/5757f9e388f7)讲解的了SDWebImage的Cache部分，这篇讲讲一下Download部分。

##Download
Download部分主要包含如下2个方法
* SDWebImageDownloader
图片下载控制类
* SDWebImageDownloaderOperation
NSOperation子类

##SDWebImageDownloader
SDWebImageDownloader主要包含以下内容：
* 初始化信息
* 相关请求信息设置与获取
* 请求图片方法
* NSURLSession相关回调

###初始化信息
SDWebImageDownloader.h中的property声明
```
//下载完成执行顺序
typedef NS_ENUM(NSInteger, SDWebImageDownloaderExecutionOrder) {
    //先进先出
    SDWebImageDownloaderFIFOExecutionOrder,
    //先进后出
    SDWebImageDownloaderLIFOExecutionOrder
};

@interface SDWebImageDownloader : NSObject
//是否压缩图片
@property (assign, nonatomic) BOOL shouldDecompressImages;
//最大下载数量
@property (assign, nonatomic) NSInteger maxConcurrentDownloads;
//当前下载数量
@property (readonly, nonatomic) NSUInteger currentDownloadCount;
//下载超时时间
@property (assign, nonatomic) NSTimeInterval downloadTimeout;
//下载策略
@property (assign, nonatomic) SDWebImageDownloaderExecutionOrder executionOrder;
```
SDWebImageDownloader.m中的Extension
```
@interface SDWebImageDownloader () <NSURLSessionTaskDelegate, NSURLSessionDataDelegate>
//下载的queue
@property (strong, nonatomic) NSOperationQueue *downloadQueue;
//上一个添加的操作
@property (weak, nonatomic) NSOperation *lastAddedOperation;
//操作类
@property (assign, nonatomic) Class operationClass;
//url请求缓存
@property (strong, nonatomic) NSMutableDictionary *URLCallbacks;
//请求头
@property (strong, nonatomic) NSMutableDictionary *HTTPHeaders;
// This queue is used to serialize the handling of the network responses of all the download operation in a single queue
//这个queue为了能否序列化处理所有网络结果的返回
@property (SDDispatchQueueSetterSementics, nonatomic) dispatch_queue_t barrierQueue;

// The session in which data tasks will run
//数据runsession
@property (strong, nonatomic) NSURLSession *session;

@end
```
相关方法实现如下：
```
//判断用了SDNetworkActivityIndicator，去替换该方法的内容
+ (void)initialize {
    // Bind SDNetworkActivityIndicator if available (download it here: http://github.com/rs/SDNetworkActivityIndicator )
    // To use it, just add #import "SDNetworkActivityIndicator.h" in addition to the SDWebImage import
    if (NSClassFromString(@"SDNetworkActivityIndicator")) {

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
        id activityIndicator = [NSClassFromString(@"SDNetworkActivityIndicator") performSelector:NSSelectorFromString(@"sharedActivityIndicator")];
#pragma clang diagnostic pop

        // Remove observer in case it was previously added.
        [[NSNotificationCenter defaultCenter] removeObserver:activityIndicator name:SDWebImageDownloadStartNotification object:nil];
        [[NSNotificationCenter defaultCenter] removeObserver:activityIndicator name:SDWebImageDownloadStopNotification object:nil];

        [[NSNotificationCenter defaultCenter] addObserver:activityIndicator
                                                 selector:NSSelectorFromString(@"startActivity")
                                                     name:SDWebImageDownloadStartNotification object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:activityIndicator
                                                 selector:NSSelectorFromString(@"stopActivity")
                                                     name:SDWebImageDownloadStopNotification object:nil];
    }
}

//单例
+ (SDWebImageDownloader *)sharedDownloader {
    static dispatch_once_t once;
    static id instance;
    dispatch_once(&once, ^{
        instance = [self new];
    });
    return instance;
}

//初始化方法
- (id)init {
    if ((self = [super init])) {
        _operationClass = [SDWebImageDownloaderOperation class];
        _shouldDecompressImages = YES;
        _executionOrder = SDWebImageDownloaderFIFOExecutionOrder;
        _downloadQueue = [NSOperationQueue new];
        _downloadQueue.maxConcurrentOperationCount = 6;
        _downloadQueue.name = @"com.hackemist.SDWebImageDownloader";
        _URLCallbacks = [NSMutableDictionary new];
#ifdef SD_WEBP
        _HTTPHeaders = [@{@"Accept": @"image/webp,image/*;q=0.8"} mutableCopy];
#else
        _HTTPHeaders = [@{@"Accept": @"image/*;q=0.8"} mutableCopy];
#endif
        _barrierQueue = dispatch_queue_create("com.hackemist.SDWebImageDownloaderBarrierQueue", DISPATCH_QUEUE_CONCURRENT);
        _downloadTimeout = 15.0;

        NSURLSessionConfiguration *sessionConfig = [NSURLSessionConfiguration defaultSessionConfiguration];
        sessionConfig.timeoutIntervalForRequest = _downloadTimeout;

        /**
         *  Create the session for this task
         *  We send nil as delegate queue so that the session creates a serial operation queue for performing all delegate
         *  method calls and completion handler calls.
         */
        //session初始化
        self.session = [NSURLSession sessionWithConfiguration:sessionConfig
                                                     delegate:self
                                                delegateQueue:nil];
    }
    return self;
}

- (void)dealloc {
    //停止session
    [self.session invalidateAndCancel];
    self.session = nil;

    //停止所有downloadqueue
    [self.downloadQueue cancelAllOperations];
    SDDispatchQueueRelease(_barrierQueue);
}
```
从这边也可以看出，SDWebImageDownloader的下载方法就是用NSOperation。使用NSOperationQueue去控制最大操作数量和取消所有操作，在NSOperation中运行NSUrlSession方法请求参数。

###相关请求信息设置与获取
相关方法如下：
```
//设置hedaer头
- (void)setValue:(NSString *)value forHTTPHeaderField:(NSString *)field {
    if (value) {
        self.HTTPHeaders[field] = value;
    }
    else {
        [self.HTTPHeaders removeObjectForKey:field];
    }
}

- (NSString *)valueForHTTPHeaderField:(NSString *)field {
    return self.HTTPHeaders[field];
}

//设置多大并发数量
- (void)setMaxConcurrentDownloads:(NSInteger)maxConcurrentDownloads {
    _downloadQueue.maxConcurrentOperationCount = maxConcurrentDownloads;
}

- (NSUInteger)currentDownloadCount {
    return _downloadQueue.operationCount;
}

- (NSInteger)maxConcurrentDownloads {
    return _downloadQueue.maxConcurrentOperationCount;
}

//设置operation的类型，可以自行再继承子类去实现
- (void)setOperationClass:(Class)operationClass {
    _operationClass = operationClass ?: [SDWebImageDownloaderOperation class];
}

//暂停下载
- (void)setSuspended:(BOOL)suspended {
    [self.downloadQueue setSuspended:suspended];
}
//取消所有下载
- (void)cancelAllDownloads {
    [self.downloadQueue cancelAllOperations];
}
```
这一部分唯一提一下的是``setOperationClass:``方法，是为了可以自己实现NSOperation的子类去完成相关请求的设置。

###请求图片方法
相关方法实现如下：
```
//请求图片方法
- (id <SDWebImageOperation>)downloadImageWithURL:(NSURL *)url options:(SDWebImageDownloaderOptions)options progress:(SDWebImageDownloaderProgressBlock)progressBlock completed:(SDWebImageDownloaderCompletedBlock)completedBlock {
    __block SDWebImageDownloaderOperation *operation;
    __weak __typeof(self)wself = self;

    //处理新请求的封装
    [self addProgressCallback:progressBlock completedBlock:completedBlock forURL:url createCallback:^{
        NSTimeInterval timeoutInterval = wself.downloadTimeout;
        if (timeoutInterval == 0.0) {
            timeoutInterval = 15.0;
        }

        // In order to prevent from potential duplicate caching (NSURLCache + SDImageCache) we disable the cache for image requests if told otherwise
        //为了防止潜在的重复cache
        NSMutableURLRequest *request = [[NSMutableURLRequest alloc] initWithURL:url cachePolicy:(options & SDWebImageDownloaderUseNSURLCache ? NSURLRequestUseProtocolCachePolicy : NSURLRequestReloadIgnoringLocalCacheData) timeoutInterval:timeoutInterval];
        //设置是否处理cookies
        request.HTTPShouldHandleCookies = (options & SDWebImageDownloaderHandleCookies);
        request.HTTPShouldUsePipelining = YES;
        //有header处理block，则调用block，返回headerfieleds
        if (wself.headersFilter) {
            request.allHTTPHeaderFields = wself.headersFilter(url, [wself.HTTPHeaders copy]);
        }
        else {
            request.allHTTPHeaderFields = wself.HTTPHeaders;
        }
        //这边的创建方式允许自己定义SDWebImageDownloaderOperation的子类进行替换
        operation = [[wself.operationClass alloc] initWithRequest:request
                                                        inSession:self.session
                                                          options:options
                                                         progress:^(NSInteger receivedSize, NSInteger expectedSize) {
                                                             SDWebImageDownloader *sself = wself;
                                                             if (!sself) return;
                                                             __block NSArray *callbacksForURL;
                                                             //这里用barrierQueue可以确保数据一致性
                                                             dispatch_sync(sself.barrierQueue, ^{
                                                                 //获取所有的同url的请求
                                                                 callbacksForURL = [sself.URLCallbacks[url] copy];
                                                             });
                                                             //遍历创建信息函数
                                                             for (NSDictionary *callbacks in callbacksForURL) {
                                                                 dispatch_async(dispatch_get_main_queue(), ^{
                                                                     //获取progressblock并调用
                                                                     SDWebImageDownloaderProgressBlock callback = callbacks[kProgressCallbackKey];
                                                                     if (callback) callback(receivedSize, expectedSize);
                                                                 });
                                                             }
                                                         }
                                                        completed:^(UIImage *image, NSData *data, NSError *error, BOOL finished) {
                                                            SDWebImageDownloader *sself = wself;
                                                            if (!sself) return;
                                                            __block NSArray *callbacksForURL;
                                                            //等待新请求插入和获取参数完结，再finish
                                                            dispatch_barrier_sync(sself.barrierQueue, ^{
                                                                callbacksForURL = [sself.URLCallbacks[url] copy];
                                                                //如果完结了，删除所有url缓存
                                                                if (finished) {
                                                                    [sself.URLCallbacks removeObjectForKey:url];
                                                                }
                                                            });
                                                            //调用completeblock
                                                            for (NSDictionary *callbacks in callbacksForURL) {
                                                                SDWebImageDownloaderCompletedBlock callback = callbacks[kCompletedCallbackKey];
                                                                if (callback) callback(image, data, error, finished);
                                                            }
                                                        }
                                                        cancelled:^{
                                                            SDWebImageDownloader *sself = wself;
                                                            if (!sself) return;
                                                            //删除url
                                                            dispatch_barrier_async(sself.barrierQueue, ^{
                                                                [sself.URLCallbacks removeObjectForKey:url];
                                                            });
                                                        }];
        //是否压缩图片
        operation.shouldDecompressImages = wself.shouldDecompressImages;
        
        //是否包含url的验证
        if (wself.urlCredential) {
            operation.credential = wself.urlCredential;
        } else if (wself.username && wself.password) {
            //有账号和密码则设置请求
            operation.credential = [NSURLCredential credentialWithUser:wself.username password:wself.password persistence:NSURLCredentialPersistenceForSession];
        }
        //设置operation请求优先级
        if (options & SDWebImageDownloaderHighPriority) {
            operation.queuePriority = NSOperationQueuePriorityHigh;
        } else if (options & SDWebImageDownloaderLowPriority) {
            operation.queuePriority = NSOperationQueuePriorityLow;
        }

        //开始请求[operation start]
        [wself.downloadQueue addOperation:operation];
        //如果是先进后出，链接依赖
        if (wself.executionOrder == SDWebImageDownloaderLIFOExecutionOrder) {
            // Emulate LIFO execution order by systematically adding new operations as last operation's dependency
            [wself.lastAddedOperation addDependency:operation];
            wself.lastAddedOperation = operation;
        }
    }];

    return operation;
}

//用来处理新请求的封装函数
- (void)addProgressCallback:(SDWebImageDownloaderProgressBlock)progressBlock completedBlock:(SDWebImageDownloaderCompletedBlock)completedBlock forURL:(NSURL *)url createCallback:(SDWebImageNoParamsBlock)createCallback {
    // The URL will be used as the key to the callbacks dictionary so it cannot be nil. If it is nil immediately call the completed block with no image or data.
    if (url == nil) {
        if (completedBlock != nil) {
            completedBlock(nil, nil, nil, NO);
        }
        return;
    }
    
    //添加progress的过程都会等待queue中的前面完成后进行
    dispatch_barrier_sync(self.barrierQueue, ^{
        BOOL first = NO;
        //如果url不存在在self.URLCallbacks中，说明该url第一次下载
        if (!self.URLCallbacks[url]) {
            self.URLCallbacks[url] = [NSMutableArray new];
            first = YES;
        }

        // Handle single download of simultaneous download request for the same URL
        //处理同时同样的url请求情况
        //将progressblock和completeblock存入dictionary，再添加到self.URLCallbacks[url]的array中，这样可以在运行完成后一起运行
        NSMutableArray *callbacksForURL = self.URLCallbacks[url];
        NSMutableDictionary *callbacks = [NSMutableDictionary new];
        if (progressBlock) callbacks[kProgressCallbackKey] = [progressBlock copy];
        if (completedBlock) callbacks[kCompletedCallbackKey] = [completedBlock copy];
        [callbacksForURL addObject:callbacks];
        self.URLCallbacks[url] = callbacksForURL;

        if (first) {
            createCallback();
        }
    });
}
```
可以理解分为两块：
* 1.请求封装参数
这一块的主要目的是存储请求的progressBlock（进度回调）和completeBlock（完成回调）到self.URLCallbacks中，并且对于同时多个请求同一个url图片的情况进行了处理。
* 2.新请求获取图片
当url为第一次请求时候，构建请求request与operation，并开始运行operation

###NSURLSession相关回调
相关方法如下：
```
#pragma mark Helper methods

//获取operationQueue中的包含此task的operation
- (SDWebImageDownloaderOperation *)operationWithTask:(NSURLSessionTask *)task {
    SDWebImageDownloaderOperation *returnOperation = nil;
    for (SDWebImageDownloaderOperation *operation in self.downloadQueue.operations) {
        if (operation.dataTask.taskIdentifier == task.taskIdentifier) {
            returnOperation = operation;
            break;
        }
    }
    return returnOperation;
}

#pragma mark NSURLSessionDataDelegate

//收到返回结果
- (void)URLSession:(NSURLSession *)session
          dataTask:(NSURLSessionDataTask *)dataTask
didReceiveResponse:(NSURLResponse *)response
 completionHandler:(void (^)(NSURLSessionResponseDisposition disposition))completionHandler {

    // Identify the operation that runs this task and pass it the delegate method
    SDWebImageDownloaderOperation *dataOperation = [self operationWithTask:dataTask];

    [dataOperation URLSession:session dataTask:dataTask didReceiveResponse:response completionHandler:completionHandler];
}

//收到返回数据
- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)dataTask didReceiveData:(NSData *)data {

    // Identify the operation that runs this task and pass it the delegate method
    SDWebImageDownloaderOperation *dataOperation = [self operationWithTask:dataTask];

    [dataOperation URLSession:session dataTask:dataTask didReceiveData:data];
}

//是否缓存reponse
- (void)URLSession:(NSURLSession *)session
          dataTask:(NSURLSessionDataTask *)dataTask
 willCacheResponse:(NSCachedURLResponse *)proposedResponse
 completionHandler:(void (^)(NSCachedURLResponse *cachedResponse))completionHandler {

    // Identify the operation that runs this task and pass it the delegate method
    SDWebImageDownloaderOperation *dataOperation = [self operationWithTask:dataTask];

    [dataOperation URLSession:session dataTask:dataTask willCacheResponse:proposedResponse completionHandler:completionHandler];
}

#pragma mark NSURLSessionTaskDelegate

//task已经完成
- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error {
    // Identify the operation that runs this task and pass it the delegate method
    SDWebImageDownloaderOperation *dataOperation = [self operationWithTask:task];

    [dataOperation URLSession:session task:task didCompleteWithError:error];
}

//task请求验证方法
- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didReceiveChallenge:(NSURLAuthenticationChallenge *)challenge completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition disposition, NSURLCredential *credential))completionHandler {

    // Identify the operation that runs this task and pass it the delegate method
    SDWebImageDownloaderOperation *dataOperation = [self operationWithTask:task];

    [dataOperation URLSession:session task:task didReceiveChallenge:challenge completionHandler:completionHandler];
}
```
实际上相关处理都已经放到NSOperation进行处理。

##SDWebImageDownloaderOperation
SDWebImageDownloaderOperation主要包含以下内容：
* 初始化相关信息
* 相关参数设置
* 开始请求与取消请求
* NSURLSession相关回调

###初始化相关信息
SDWebImageDownloaderOperation.h的property声明
```
@interface SDWebImageDownloaderOperation : NSOperation <SDWebImageOperation, NSURLSessionTaskDelegate, NSURLSessionDataDelegate>
//operation的请求
@property (strong, nonatomic, readonly) NSURLRequest *request;
//operation的task
@property (strong, nonatomic, readonly) NSURLSessionTask *dataTask;
//是否压缩图片
@property (assign, nonatomic) BOOL shouldDecompressImages;
//当收到`-connection:didReceiveAuthenticationChallenge:`的验证
@property (nonatomic, strong) NSURLCredential *credential;
//下载方式
@property (assign, nonatomic, readonly) SDWebImageDownloaderOptions options;
//文件大小
@property (assign, nonatomic) NSInteger expectedSize;
//operation的返回
@property (strong, nonatomic) NSURLResponse *response;
```
SDWebImageDownloaderOperation.m中的extension
```
@interface SDWebImageDownloaderOperation ()

@property (copy, nonatomic) SDWebImageDownloaderProgressBlock progressBlock;
@property (copy, nonatomic) SDWebImageDownloaderCompletedBlock completedBlock;
@property (copy, nonatomic) SDWebImageNoParamsBlock cancelBlock;

@property (assign, nonatomic, getter = isExecuting) BOOL executing;
@property (assign, nonatomic, getter = isFinished) BOOL finished;
@property (strong, nonatomic) NSMutableData *imageData;

// This is weak because it is injected by whoever manages this session. If this gets nil-ed out, we won't be able to run
// the task associated with this operation
//因为是被downloader持有，所以是weak
@property (weak, nonatomic) NSURLSession *unownedSession;
// This is set if we're using not using an injected NSURLSession. We're responsible of invalidating this one
//如果不用设置的nsurlsession，需要自己去吃油一个
@property (strong, nonatomic) NSURLSession *ownedSession;
//sessiontask
@property (strong, nonatomic, readwrite) NSURLSessionTask *dataTask;
//当前线程
@property (strong, atomic) NSThread *thread;

#if TARGET_OS_IPHONE && __IPHONE_OS_VERSION_MAX_ALLOWED >= __IPHONE_4_0
@property (assign, nonatomic) UIBackgroundTaskIdentifier backgroundTaskId;
#endif

@end

@implementation SDWebImageDownloaderOperation {
    //图片长,宽
    size_t width, height;
    //图片方向
    UIImageOrientation orientation;
    //是否图片是从cache读出
    BOOL responseFromCached;
}
```
初始化相关方法：
```
- (id)initWithRequest:(NSURLRequest *)request
            inSession:(NSURLSession *)session
              options:(SDWebImageDownloaderOptions)options
             progress:(SDWebImageDownloaderProgressBlock)progressBlock
            completed:(SDWebImageDownloaderCompletedBlock)completedBlock
            cancelled:(SDWebImageNoParamsBlock)cancelBlock {
    if ((self = [super init])) {
        _request = [request copy];
        _shouldDecompressImages = YES;
        _options = options;
        _progressBlock = [progressBlock copy];
        _completedBlock = [completedBlock copy];
        _cancelBlock = [cancelBlock copy];
        _executing = NO;
        _finished = NO;
        _expectedSize = 0;
        _unownedSession = session;
        responseFromCached = YES; // Initially wrong until `- URLSession:dataTask:willCacheResponse:completionHandler: is called or not called
    }
    return self;
}
```
同样，从初始化的参数可以看出，在SDWebImageDownloaderOperation中，通过NSURLRequest去生成一个NSURLSessionTask，然后建立链接，获取信息。

###相关参数设置
相关实现如下：
```
//重写set方法，并且设置kvo的观察回调
- (void)setFinished:(BOOL)finished {
    [self willChangeValueForKey:@"isFinished"];
    _finished = finished;
    [self didChangeValueForKey:@"isFinished"];
}

//重写excute方法，并且设置kvo的观察回调
- (void)setExecuting:(BOOL)executing {
    [self willChangeValueForKey:@"isExecuting"];
    _executing = executing;
    [self didChangeValueForKey:@"isExecuting"];
}

//是否允许并发
- (BOOL)isConcurrent {
    return YES;
}
```
这里重写的时候还注意到了原来相关属性的KVO值。

###开始请求与取消请求
主要就是重写NSOperation的``start``和``cancel``方法，其中``cancel``方法还是``SDWebImageOperation``的回调。
相关方法如下：
```
//重新父类start开始方法
- (void)start {
    //加锁
    @synchronized (self) {
        //如果已经取消，则取消
        if (self.isCancelled) {
            self.finished = YES;
            //重置数据
            [self reset];
            return;
        }

#if TARGET_OS_IPHONE && __IPHONE_OS_VERSION_MAX_ALLOWED >= __IPHONE_4_0
        //在iOS4以上，会去运行后台方法，等待完成后，取消相关下载
        Class UIApplicationClass = NSClassFromString(@"UIApplication");
        BOOL hasApplication = UIApplicationClass && [UIApplicationClass respondsToSelector:@selector(sharedApplication)];
        if (hasApplication && [self shouldContinueWhenAppEntersBackground]) {
            __weak __typeof__ (self) wself = self;
            UIApplication * app = [UIApplicationClass performSelector:@selector(sharedApplication)];
            self.backgroundTaskId = [app beginBackgroundTaskWithExpirationHandler:^{
                __strong __typeof (wself) sself = wself;

                if (sself) {
                    [sself cancel];

                    [app endBackgroundTask:sself.backgroundTaskId];
                    sself.backgroundTaskId = UIBackgroundTaskInvalid;
                }
            }];
        }
#endif
        NSURLSession *session = self.unownedSession;
        //没有unowndSession，则创建一个
        if (!self.unownedSession) {
            NSURLSessionConfiguration *sessionConfig = [NSURLSessionConfiguration defaultSessionConfiguration];
            sessionConfig.timeoutIntervalForRequest = 15;
            
            /**
             *  Create the session for this task
             *  We send nil as delegate queue so that the session creates a serial operation queue for performing all delegate
             *  method calls and completion handler calls.
             */
            self.ownedSession = [NSURLSession sessionWithConfiguration:sessionConfig
                                                              delegate:self
                                                         delegateQueue:nil];
            session = self.ownedSession;
        }
        //创建NSURLSessionDataTask
        self.dataTask = [session dataTaskWithRequest:self.request];
        //代表是否在执行
        self.executing = YES;
        //创建thread，目的是能够在该方法开始后，取消方法在开始方法结束进行调用
        self.thread = [NSThread currentThread];
    }
    
    //开始执行task
    [self.dataTask resume];
    
    if (self.dataTask) {
        //初始化的进度block
        if (self.progressBlock) {
            self.progressBlock(0, NSURLResponseUnknownLength);
        }
        //发一个开始下载的notification
        dispatch_async(dispatch_get_main_queue(), ^{
            [[NSNotificationCenter defaultCenter] postNotificationName:SDWebImageDownloadStartNotification object:self];
        });
    }
    else {
        //不存在request就直接返回下载失败
        if (self.completedBlock) {
            self.completedBlock(nil, nil, [NSError errorWithDomain:NSURLErrorDomain code:0 userInfo:@{NSLocalizedDescriptionKey : @"Connection can't be initialized"}], YES);
        }
    }

#if TARGET_OS_IPHONE && __IPHONE_OS_VERSION_MAX_ALLOWED >= __IPHONE_4_0
    //直接停止后台下载动作
    Class UIApplicationClass = NSClassFromString(@"UIApplication");
    if(!UIApplicationClass || ![UIApplicationClass respondsToSelector:@selector(sharedApplication)]) {
        return;
    }
    if (self.backgroundTaskId != UIBackgroundTaskInvalid) {
        UIApplication * app = [UIApplication performSelector:@selector(sharedApplication)];
        [app endBackgroundTask:self.backgroundTaskId];
        self.backgroundTaskId = UIBackgroundTaskInvalid;
    }
#endif
}

//SDWebImageOperation的deelgate
- (void)cancel {
    @synchronized (self) {
        //有self.thread说明该operation已经开始下载了，需要把cancel方法放到下载的同一个线程，并且等待下载完成后cancel
        if (self.thread) {
            [self performSelector:@selector(cancelInternalAndStop) onThread:self.thread withObject:nil waitUntilDone:NO];
        }
        else {
            [self cancelInternal];
        }
    }
}
//线程cancel方法
- (void)cancelInternalAndStop {
    if (self.isFinished) return;
    [self cancelInternal];
}

//停止下载
- (void)cancelInternal {
    if (self.isFinished) return;
    [super cancel];
    if (self.cancelBlock) self.cancelBlock();

    //数据请求取消
    if (self.dataTask) {
        [self.dataTask cancel];
        //发送stop notification
        dispatch_async(dispatch_get_main_queue(), ^{
            [[NSNotificationCenter defaultCenter] postNotificationName:SDWebImageDownloadStopNotification object:self];
        });

        // As we cancelled the connection, its callback won't be called and thus won't
        // maintain the isFinished and isExecuting flags.
        //停止请求后，需要去设置相关方法
        if (self.isExecuting) self.executing = NO;
        if (!self.isFinished) self.finished = YES;
    }

    [self reset];
}

//下载完毕
- (void)done {
    self.finished = YES;
    self.executing = NO;
    [self reset];
}

//重置信息
- (void)reset {
    self.cancelBlock = nil;
    self.completedBlock = nil;
    self.progressBlock = nil;
    self.dataTask = nil;
    self.imageData = nil;
    self.thread = nil;
    if (self.ownedSession) {
        [self.ownedSession invalidateAndCancel];
        self.ownedSession = nil;
    }
}
```
这里比较有趣的是``self.thread``的妙用，如果存在``self.thread``说明operation已经start，那么cancel已经无效，必须等待请求完成后，才能去cancel它，防止出现问题。这边用``self.thread``就可以达成这样的目的。

###NSURLSession相关回调
相关方法如下：

```
#pragma mark NSURLSessionDataDelegate

//收到请求回复
- (void)URLSession:(NSURLSession *)session
          dataTask:(NSURLSessionDataTask *)dataTask
didReceiveResponse:(NSURLResponse *)response
 completionHandler:(void (^)(NSURLSessionResponseDisposition disposition))completionHandler {
    
    //'304 Not Modified' is an exceptional one
    //除了304以外<400的工程情况
    if (![response respondsToSelector:@selector(statusCode)] || ([((NSHTTPURLResponse *)response) statusCode] < 400 && [((NSHTTPURLResponse *)response) statusCode] != 304)) {
        NSInteger expected = response.expectedContentLength > 0 ? (NSInteger)response.expectedContentLength : 0;
        //设置接受数据大小
        self.expectedSize = expected;
        //初始化progress过程
        if (self.progressBlock) {
            self.progressBlock(0, expected);
        }
        
        //创建能够接受的图片大小数据
        self.imageData = [[NSMutableData alloc] initWithCapacity:expected];
        self.response = response;
        dispatch_async(dispatch_get_main_queue(), ^{
            [[NSNotificationCenter defaultCenter] postNotificationName:SDWebImageDownloadReceiveResponseNotification object:self];
        });
    }
    else {
        NSUInteger code = [((NSHTTPURLResponse *)response) statusCode];
        
        //This is the case when server returns '304 Not Modified'. It means that remote image is not changed.
        //In case of 304 we need just cancel the operation and return cached image from the cache.
        //304说明图片没有变化，停止operation然后返回cache
        if (code == 304) {
            [self cancelInternal];
        } else {
            //其他情况停止请求
            [self.dataTask cancel];
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            [[NSNotificationCenter defaultCenter] postNotificationName:SDWebImageDownloadStopNotification object:self];
        });
        //完成block，返回错误
        if (self.completedBlock) {
            self.completedBlock(nil, nil, [NSError errorWithDomain:NSURLErrorDomain code:[((NSHTTPURLResponse *)response) statusCode] userInfo:nil], YES);
        }
        //结束
        [self done];
    }
    
    //回调response允许调用
    if (completionHandler) {
        completionHandler(NSURLSessionResponseAllow);
    }
}

//收到请求数据，分进度
- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)dataTask didReceiveData:(NSData *)data {
    //数据添加到imageData缓存
    [self.imageData appendData:data];
    
    //如果需要显示进度
    if ((self.options & SDWebImageDownloaderProgressiveDownload) && self.expectedSize > 0 && self.completedBlock) {
        // The following code is from http://www.cocoaintheshell.com/2011/05/progressive-images-download-imageio/
        // Thanks to the author @Nyx0uf

        // Get the total bytes downloaded
        //获得下载的图片大小
        const NSInteger totalSize = self.imageData.length;

        // Update the data source, we must pass ALL the data, not just the new bytes
        //更新数据，需要把所有数据一同更新
        CGImageSourceRef imageSource = CGImageSourceCreateWithData((__bridge CFDataRef)self.imageData, NULL);
        
        //为0说明是第一段数据
        if (width + height == 0) {
            CFDictionaryRef properties = CGImageSourceCopyPropertiesAtIndex(imageSource, 0, NULL);
            if (properties) {
                NSInteger orientationValue = -1;
                CFTypeRef val = CFDictionaryGetValue(properties, kCGImagePropertyPixelHeight);
                //获得图片高
                if (val) CFNumberGetValue(val, kCFNumberLongType, &height);
                val = CFDictionaryGetValue(properties, kCGImagePropertyPixelWidth);
                //获得图片宽
                if (val) CFNumberGetValue(val, kCFNumberLongType, &width);
                val = CFDictionaryGetValue(properties, kCGImagePropertyOrientation);
                //获得图片旋转方向
                if (val) CFNumberGetValue(val, kCFNumberNSIntegerType, &orientationValue);
                CFRelease(properties);

                // When we draw to Core Graphics, we lose orientation information,
                // which means the image below born of initWithCGIImage will be
                // oriented incorrectly sometimes. (Unlike the image born of initWithData
                // in didCompleteWithError.) So save it here and pass it on later.
                //当我们绘制Core Graphic，我们会失去图片方向信息
                //着意味着用initWithCGIImage将会有的时候并不正确，（不像在didCompleteWithError里用initWithData),所以保存信息
                orientation = [[self class] orientationFromPropertyValue:(orientationValue == -1 ? 1 : orientationValue)];
            }

        }
        
        //不是第一段数据，并且没有下载完毕
        if (width + height > 0 && totalSize < self.expectedSize) {
            // Create the image
            //创建图片来源
            CGImageRef partialImageRef = CGImageSourceCreateImageAtIndex(imageSource, 0, NULL);

#ifdef TARGET_OS_IPHONE
            // Workaround for iOS anamorphic image
            //处理iOS失真图片，这边的处理方式有写看不大懂
            if (partialImageRef) {
                const size_t partialHeight = CGImageGetHeight(partialImageRef);
                CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
                CGContextRef bmContext = CGBitmapContextCreate(NULL, width, height, 8, width * 4, colorSpace, kCGBitmapByteOrderDefault | kCGImageAlphaPremultipliedFirst);
                CGColorSpaceRelease(colorSpace);
                if (bmContext) {
                    CGContextDrawImage(bmContext, (CGRect){.origin.x = 0.0f, .origin.y = 0.0f, .size.width = width, .size.height = partialHeight}, partialImageRef);
                    CGImageRelease(partialImageRef);
                    partialImageRef = CGBitmapContextCreateImage(bmContext);
                    CGContextRelease(bmContext);
                }
                else {
                    CGImageRelease(partialImageRef);
                    partialImageRef = nil;
                }
            }
#endif
            //如果有了图片数据
            if (partialImageRef) {
                //获取图片
                UIImage *image = [UIImage imageWithCGImage:partialImageRef scale:1 orientation:orientation];
                //获得key
                NSString *key = [[SDWebImageManager sharedManager] cacheKeyForURL:self.request.URL];
                //获得适合屏幕的图片
                UIImage *scaledImage = [self scaledImageForKey:key image:image];
                if (self.shouldDecompressImages) {
                    //压缩图片
                    image = [UIImage decodedImageWithImage:scaledImage];
                }
                else {
                    image = scaledImage;
                }
                CGImageRelease(partialImageRef);
                //返回完成结果
                dispatch_main_sync_safe(^{
                    if (self.completedBlock) {
                        self.completedBlock(image, nil, nil, NO);
                    }
                });
            }
        }

        CFRelease(imageSource);
    }
    
    //显示阶段
    if (self.progressBlock) {
        self.progressBlock(self.imageData.length, self.expectedSize);
    }
}

- (void)URLSession:(NSURLSession *)session
          dataTask:(NSURLSessionDataTask *)dataTask
 willCacheResponse:(NSCachedURLResponse *)proposedResponse
 completionHandler:(void (^)(NSCachedURLResponse *cachedResponse))completionHandler {

    //说明结果没有从cache读取
    responseFromCached = NO; // If this method is called, it means the response wasn't read from cache
    NSCachedURLResponse *cachedResponse = proposedResponse;

    //如果是放弃cache的模式
    if (self.request.cachePolicy == NSURLRequestReloadIgnoringLocalCacheData) {
        // Prevents caching of responses
        cachedResponse = nil;
    }
    //回调结果cachedResponse
    if (completionHandler) {
        completionHandler(cachedResponse);
    }
}

#pragma mark NSURLSessionTaskDelegate

//完成请求
- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error {
    @synchronized(self) {
        //完成后的数据设置
        self.thread = nil;
        self.dataTask = nil;
        //停止notification
        dispatch_async(dispatch_get_main_queue(), ^{
            [[NSNotificationCenter defaultCenter] postNotificationName:SDWebImageDownloadStopNotification object:self];
            if (!error) {
                [[NSNotificationCenter defaultCenter] postNotificationName:SDWebImageDownloadFinishNotification object:self];
            }
        });
    }
    
    //如果有error，将error返回
    if (error) {
        if (self.completedBlock) {
            self.completedBlock(nil, nil, error, YES);
        }
    } else {
        //如果存在完成block
        SDWebImageDownloaderCompletedBlock completionBlock = self.completedBlock;
        
        if (completionBlock) {
            /**
             *  See #1608 and #1623 - apparently, there is a race condition on `NSURLCache` that causes a crash
             *  Limited the calls to `cachedResponseForRequest:` only for cases where we should ignore the cached response
             *    and images for which responseFromCached is YES (only the ones that cannot be cached).
             *  Note: responseFromCached is set to NO inside `willCacheResponse:`. This method doesn't get called for large images or images behind authentication 
             */
            //查看#1608和#1623的pull request。显然，这里会有一个罕见的NSURLCache的crash
            //限制了调用cachedResponseForRequest，只有当responseFromCached为yes的时候，我们应该去忽略缓存的reponse和图片
            //记录：当responseFromCached在`willCacheResponse:`设置为no。这个方法不会在大型图片和验证图片的时候到靠用
            if (self.options & SDWebImageDownloaderIgnoreCachedResponse && responseFromCached && [[NSURLCache sharedURLCache] cachedResponseForRequest:self.request]) {
                completionBlock(nil, nil, nil, YES);
            } else if (self.imageData) {
                //初始化图片
                UIImage *image = [UIImage sd_imageWithData:self.imageData];
                //缓存的url的key
                NSString *key = [[SDWebImageManager sharedManager] cacheKeyForURL:self.request.URL];
                //适应屏幕设置
                image = [self scaledImageForKey:key image:image];
                
                // Do not force decoding animated GIFs
                //如果不是GIF
                if (!image.images) {
                    if (self.shouldDecompressImages) {
                        //压缩图片
                        image = [UIImage decodedImageWithImage:image];
                    }
                }
                //图片大小为0，则报错
                if (CGSizeEqualToSize(image.size, CGSizeZero)) {
                    completionBlock(nil, nil, [NSError errorWithDomain:SDWebImageErrorDomain code:0 userInfo:@{NSLocalizedDescriptionKey : @"Downloaded image has 0 pixels"}], YES);
                }
                else {
                    //完成整个图片处理
                    completionBlock(image, self.imageData, nil, YES);
                }
            } else {
                //图片为空
                completionBlock(nil, nil, [NSError errorWithDomain:SDWebImageErrorDomain code:0 userInfo:@{NSLocalizedDescriptionKey : @"Image data is nil"}], YES);
            }
        }
    }
    
    self.completionBlock = nil;
    [self done];
}

//处理请求特殊权限验证
- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didReceiveChallenge:(NSURLAuthenticationChallenge *)challenge completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition disposition, NSURLCredential *credential))completionHandler {
    
    NSURLSessionAuthChallengeDisposition disposition = NSURLSessionAuthChallengePerformDefaultHandling;
    __block NSURLCredential *credential = nil;
    
    //需要信任该服务
    if ([challenge.protectionSpace.authenticationMethod isEqualToString:NSURLAuthenticationMethodServerTrust]) {
        //不需要信任，则走默认处理
        if (!(self.options & SDWebImageDownloaderAllowInvalidSSLCertificates)) {
            disposition = NSURLSessionAuthChallengePerformDefaultHandling;
        } else {
            //设置站点为信任
            credential = [NSURLCredential credentialForTrust:challenge.protectionSpace.serverTrust];
            disposition = NSURLSessionAuthChallengeUseCredential;
        }
    } else {
        //有一些错误
        if ([challenge previousFailureCount] == 0) {
            if (self.credential) {
                //使用credential
                credential = self.credential;
                disposition = NSURLSessionAuthChallengeUseCredential;
            } else {
                disposition = NSURLSessionAuthChallengeCancelAuthenticationChallenge;
            }
        } else {
            disposition = NSURLSessionAuthChallengeCancelAuthenticationChallenge;
        }
    }
    //回调验证信息
    if (completionHandler) {
        completionHandler(disposition, credential);
    }
}

#pragma mark Helper methods

//返回图片方向
+ (UIImageOrientation)orientationFromPropertyValue:(NSInteger)value {
    switch (value) {
        case 1:
            return UIImageOrientationUp;
        case 3:
            return UIImageOrientationDown;
        case 8:
            return UIImageOrientationLeft;
        case 6:
            return UIImageOrientationRight;
        case 2:
            return UIImageOrientationUpMirrored;
        case 4:
            return UIImageOrientationDownMirrored;
        case 5:
            return UIImageOrientationLeftMirrored;
        case 7:
            return UIImageOrientationRightMirrored;
        default:
            return UIImageOrientationUp;
    }
}

- (UIImage *)scaledImageForKey:(NSString *)key image:(UIImage *)image {
    return SDScaledImageForKey(key, image);
}

//是否应该在后台运行
- (BOOL)shouldContinueWhenAppEntersBackground {
    return self.options & SDWebImageDownloaderContinueInBackground;
}
```
这边的代码比较多，比较有意思的地方主要在于以下两点:
* 1.图片的处理
在下载过程当中，就对已经接受数据的信息并且转换成了模糊化的图片，并且回调了``self.completedBlock(image, nil, nil, NO);``，说明可以做到图片逐渐清晰的这种效果。
* 2.验证信息的方式
``didReceiveChallenge``方法代表在请求时候，遇到一些验证。

##总结
总的来说，Downloader有以下优点：
* 1.设计上支持并发的下载同一个url的图片，并且有正确的回调
* 2.通过barrier去确保获取数据不会与设置数据有冲突
* 3.妙用thread去达到nsoperation无法取消但可以等待完成后取消的结果
* 4.神奇的图片处理，一开始获取图片的高、宽、方向，然后通过神奇的方法，进行图片展示，这个方法到现在为止我还是没看懂它是如何处理的，如果有懂的人麻烦指点一下
* 5.NSURLSession的使用（在这之前我只用过NSURLConnection，对ios7出来的NSURLSession确实了解不多）
* 6.对于验证请求的处理

***
更新：关于图片处理这块，在utils的解码中，有详细一些的标注，可以稍微用来理解这里的图片处理。

##参考资料
1.[SDWebImage源码浅析](http://joakimliu.github.io/2015/11/15/Resolve-The-SourceCode-Of-SDWebImage/)
2.[Apple Guide:URL Session Programming Guide](https://developer.apple.com/library/content/documentation/Cocoa/Conceptual/URLLoadingSystem/Articles/UsingNSURLSession.html#//apple_ref/doc/uid/TP40013509-SW3)
