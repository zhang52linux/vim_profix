import scrapy
import re
from scrapy.linkextractors import LinkExtractor  # Response里的链接提取规则
from scrapy.spiders import CrawlSpider, Rule
from scrapy_redis.spiders import RedisCrawlSpider
from mycrawl.items import MycrawlItem


class MyfirstSpider(RedisCrawlSpider):
    name = 'myFirst'
    allowed_domains = ['ygdy8.net']
    # start_urls = ['https://www.ygdy8.net/']
    # callback 提取到link给那个函数解析,follow 跟进爬取
    redis_key = "movie_url"

    rules = (
        Rule(LinkExtractor(allow=r'index.html', deny=r'game')),
        Rule(LinkExtractor(allow=r'list_\d+_\d+.html'),follow=True),
        Rule(LinkExtractor(allow=r'/\d+/\d+.html'),callback='parse_item',follow=True),
    )

    def parse_item(self, response):
        # print(response.status)
        # print(response.url)
        # print(response.headers)
        item = MycrawlItem()
        pattern = re.compile(r'<a href="(.*?)">ftp')
        movie_url_list = pattern.findall(response.text)
        for it in movie_url_list:
            item["movie_url"] = it
            yield item
            # print(it)
