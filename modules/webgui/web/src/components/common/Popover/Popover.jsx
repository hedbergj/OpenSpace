import React, { Component } from 'react';
import PropTypes from 'prop-types';

import Icon from '../Icon/Icon';
import styles from './Popover.scss';

const findStyles = arr => arr.split(' ')
  .map(style => styles[style])
  .join(' ');

class Popover extends Component {
  get arrowStyle() {
    return findStyles(this.props.arrow);
  }

  get styles() {
    return findStyles(this.props.styles);
  }

  render() {
    return (
      <section className={`${styles.popover} ${this.arrowStyle} ${this.styles}`}>
        { this.props.title &&
          (<header>
            <div className={styles.title}>
              { this.props.title }
            </div>

            { this.props.closeCallback &&
              (<button onClick={this.props.closeCallback} className={styles.close}>
                <Icon icon="close" className="small" />
              </button>) }
          </header>)}
        { this.props.children }
      </section>
    );
  }
}

Popover.propTypes = {
  arrow: PropTypes.string,
  children: PropTypes.node.isRequired,
  closeCallback: PropTypes.func,
  styles: PropTypes.string,
  title: PropTypes.string,
};

Popover.defaultProps = {
  arrow: 'arrow bottom center',
  closeCallback: null,
  styles: '',
  title: null,
};

Popover.styles = styles;

export default Popover;