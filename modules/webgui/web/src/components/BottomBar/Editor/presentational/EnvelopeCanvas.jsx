import React from 'react'
import PropTypes from 'prop-types'
import styles from '../style/Envelope.scss'
import GraphBody from '../../../common/Graph/GraphBody'
import Point from './Point'

const pointsForEnvelopeGraph = (data) => {
  console.log(data)
    let convertedData = [];
    data.forEach(function(point) {
      let tmpObject = Object.assign({},
        {x: point.position.x + 10,
         y: 600 - point.position.y - 10,
         color: point.color}
        )
      convertedData.push(tmpObject);
    })
    return convertedData;
  }

const EnvelopeCanvas = ({
  handleClick,
  handleDrag,
  height,
  width,
  active,
  points,
}) => (
    <div className={styles.Envelope}>
      <svg className={styles.Line} height={height} width={width + 10}>
        <GraphBody
         UseLinearGradient={true}
         points={pointsForEnvelopeGraph(points)}
         x={0}
         y={600}
         width={width}
         fillOpacity={"0"}
         strokeWidth={2}
        />
      </svg>
     {points.map((point, index) =>
      <Point className={styles.Envelope}
        key={point.id}
        handleClick={() => handleClick(point.id)}
        handleDrag={(e, ui) => handleDrag(e, ui, index)}
        height={height}
        width={width}
        {...point}
        active={(point.active || active) ? true : false}
      />
      )}
    </div>
)
EnvelopeCanvas.propTypes = {
  HandleDrag: PropTypes.func.isRequired,
  handleClick: PropTypes.func.isRequired,
  points: PropTypes.arrayOf(
    PropTypes.shape({
      id: PropTypes.number.isRequired,
      position: PropTypes.shape({
              x: PropTypes.number.isRequired,
              y: PropTypes.number.isRequired,
            }).isRequired,
      anchor: PropTypes.bool.isRequired,
      color: PropTypes.string.isRequired,
    }).isRequired,
  ).isRequired,
  height: PropTypes.number.isRequired,
  width: PropTypes.number.isRequired,
  active: PropTypes.bool.isRequired,
}
export default EnvelopeCanvas
