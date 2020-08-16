# Define here the models for your scraped items
#
# See documentation in:
# https://docs.scrapy.org/en/latest/topics/items.html

import scrapy


class MycrawlItem(scrapy.Item):
    # define the fields for your item here like:
    # movie_title = scrapy.Field()
    # movie_info = scrapy.Field()
    # movie_stars = scrapy.Field()
    # movie_description = scrapy.Field()
    movie_url = scrapy.Field()