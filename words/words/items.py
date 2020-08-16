# -*- coding: utf-8 -*-

# Define here the models for your scraped items
#
# See documentation in:
# https://docs.scrapy.org/en/latest/topics/items.html

import scrapy


class WordsItem(scrapy.Item):
   name = scrapy.Field()
   pinyin = scrapy.Field()
   bihua = scrapy.Field()
   bushou = scrapy.Field()
   jiben = scrapy.Field()
   xiangxi1 = scrapy.Field()
   xiangxi2 = scrapy.Field()
   xiangxi3 = scrapy.Field()
   hanyu = scrapy.Field()
