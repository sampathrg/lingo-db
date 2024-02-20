select d_year, c_city, s_city, sum(cast(lo_revenue as int8))
as revenue
from customer, lineorder, supplier, date
where lo_custkey = c_custkey
and lo_suppkey = s_suppkey
and lo_orderdate = d_datekey
and (c_city='UNITED KI1'
   or c_city='UNITED KI5')
and (s_city='UNITED KI1'
   or s_city='UNITED KI5')
and d_yearmonth = 'Dec1997'
group by c_city, s_city, d_year
