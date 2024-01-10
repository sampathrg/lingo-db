select sum(cast(lo_extendedprice as int8) * cast(lo_discount as int8)) as revenue
from lineorder, date
where lo_orderdate = d_datekey
and d_year = 1993
and lo_discount between 1 and 3
and lo_quantity < 25
