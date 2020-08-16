# -*- coding: utf-8 -*-
from scrapy_redis.spiders import RedisSpider
import scrapy
from Movie.items import MovieItem



class MoviespiderSpider(RedisSpider):
    name = 'MovieSpider'
    allowed_domains = ['ximalaya.com']
    # start_urls = ['https://www.ximalaya.com/youshengshu/12576446/p1']
    redis_key = 'movie_url'
    # lpush movie_url https://www.ximalaya.com/youshengshu/12576446/p1

    def parse(self, response):
        list = response.xpath('//div[@id="anchor_sound_list"]/div[2]/ul/li/div[2]/a/@href').getall()
        for it in list:
            item = MovieItem(address='https://www.ximalaya.com'+it)
            yield item
            print('https://www.ximalaya.com'+it)
        print('*'*50)
        next = response.xpath('//div[@id="anchor_sound_list"]/div[2]/div/nav/ul/li[last()]/a/@href').get()

        yield scrapy.Request('https://www.ximalaya.com'+next)
