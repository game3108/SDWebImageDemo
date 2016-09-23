/*
 * This file is part of the SDWebImage package.
 * (c) Olivier Poitrey <rs@dailymotion.com>
 *
 * For the full copyright and license information, please view the LICENSE
 * file that was distributed with this source code.
 */

#import <Foundation/Foundation.h>
#import "SDWebImageManager.h"

@class SDWebImagePrefetcher;

@protocol SDWebImagePrefetcherDelegate <NSObject>

@optional

/**
 * Called when an image was prefetched.
 *
 * @param imagePrefetcher The current image prefetcher
 * @param imageURL        The image url that was prefetched
 * @param finishedCount   The total number of images that were prefetched (successful or not)
 * @param totalCount      The total number of images that were to be prefetched
 */

/**
 当一张图片预加载的时候调用

 @param imagePrefetcher 当前图片的预加载类
 @param imageURL        图片url
 @param finishedCount   已经预加载的数量
 @param totalCount      总共预加载的图片数量
 */
- (void)imagePrefetcher:(SDWebImagePrefetcher *)imagePrefetcher didPrefetchURL:(NSURL *)imageURL finishedCount:(NSUInteger)finishedCount totalCount:(NSUInteger)totalCount;

/**
 * Called when all images are prefetched.
 * @param imagePrefetcher The current image prefetcher
 * @param totalCount      The total number of images that were prefetched (whether successful or not)
 * @param skippedCount    The total number of images that were skipped
 */

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

/**
 * Prefetch some URLs in the cache for future use. Images are downloaded in low priority.
 */
@interface SDWebImagePrefetcher : NSObject

/**
 *  The web image manager
 */
//图片下载核心类
@property (strong, nonatomic, readonly) SDWebImageManager *manager;

/**
 * Maximum number of URLs to prefetch at the same time. Defaults to 3.
 */
//同时预加载的最大数量，默认为3
@property (nonatomic, assign) NSUInteger maxConcurrentDownloads;

/**
 * SDWebImageOptions for prefetcher. Defaults to SDWebImageLowPriority.
 */
//预加载的下载设置
@property (nonatomic, assign) SDWebImageOptions options;

/**
 * Queue options for Prefetcher. Defaults to Main Queue.
 */
//预加载queue
@property (nonatomic, assign) dispatch_queue_t prefetcherQueue;
//delegate
@property (weak, nonatomic) id <SDWebImagePrefetcherDelegate> delegate;

/**
 * Return the global image prefetcher instance.
 */
//单例
+ (SDWebImagePrefetcher *)sharedImagePrefetcher;

/**
 * Allows you to instantiate a prefetcher with any arbitrary image manager.
 */
//允许你实例化一个有任意图像处理的管理器
- (id)initWithImageManager:(SDWebImageManager *)manager;

/**
 * Assign list of URLs to let SDWebImagePrefetcher to queue the prefetching,
 * currently one image is downloaded at a time,
 * and skips images for failed downloads and proceed to the next image in the list
 *
 * @param urls list of URLs to prefetch
 */

/**
 列所有需要预加载的url
 同时1张图图片只会下载1次
 跳过下载失败的图片并列岛下一个列表中

 @param urls 需要预加载的url列表
 */
- (void)prefetchURLs:(NSArray *)urls;

/**
 * Assign list of URLs to let SDWebImagePrefetcher to queue the prefetching,
 * currently one image is downloaded at a time,
 * and skips images for failed downloads and proceed to the next image in the list
 *
 * @param urls            list of URLs to prefetch
 * @param progressBlock   block to be called when progress updates; 
 *                        first parameter is the number of completed (successful or not) requests, 
 *                        second parameter is the total number of images originally requested to be prefetched
 * @param completionBlock block to be called when prefetching is completed
 *                        first param is the number of completed (successful or not) requests,
 *                        second parameter is the number of skipped requests
 */

/**
 列所有需要预加载的url
 同时1张图图片只会下载1次
 跳过下载失败的图片并列岛下一个列表中

 @param urls            需要预加载的url列表
 @param progressBlock   进度block
 @param completionBlock 完成block
 */
- (void)prefetchURLs:(NSArray *)urls progress:(SDWebImagePrefetcherProgressBlock)progressBlock completed:(SDWebImagePrefetcherCompletionBlock)completionBlock;

/**
 * Remove and cancel queued list
 */
//移除并取消加载列表
- (void)cancelPrefetching;


@end
