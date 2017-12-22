import React from 'react';
import PropTypes from 'prop-types';
import styles from '../style/Histogram.scss'

const normalizeBetweenValues = (value, min, max, width) => {
    return (min + (max - min) * (value / width));
  }

const PointPosition = ({
  points,
  width,
  height,
  value,
  minValue,
  maxValue,
}) => (
    <div >
    {(points != undefined) && (
      points.map((point, index) =>
      <div className={styles.Line} key={index}>
      <svg width={width + 100} height={height + 100}>
      <line {...point} strokeDasharray={5, 5}  stroke={"white"} strokeWidth={2}></line>
      <text x={point.x1 - 10} y={point.y2 + 30} fontFamily={"Verdana"} fontSize={10} fill={"white"}>
        {normalizeBetweenValues(point.x1, minValue, maxValue, width).toPrecision(5).toString()}
      </text>
      </svg>
      </div>
    ))}
  </div>
  );
export default PointPosition;
